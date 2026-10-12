#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include "bootlog.h"
#include "settings.h"
#include "ui.h"

namespace net {

constexpr uint32_t CONNECT_MS = 45000;      // first connection after boot; after that, setup opens
constexpr uint32_t JOIN_MS = 25000;         // trying a network entered on the setup page
constexpr uint32_t RETRY_EVERY_MS = 60000;  // while setup waits, retry the saved network when nobody is on it
constexpr uint32_t SETUP_IDLE_MS = 10 * 60000;  // setup that was asked for gives up after this long with no phone
constexpr uint8_t AP_CHANNEL = 1;
// The ESP32-C3 SuperMini's antenna is poorly matched. At full power its own setup network shows up in a phone's list
// but nothing can join it; joining a router still works. 8.5 dBm is the usual fix and is plenty for
// a phone next to the stand. Only setup uses it: a restart goes back to full power for the home network.
constexpr wifi_power_t SETUP_TX_POWER = WIFI_POWER_8_5dBm;

enum class Mode { Connecting, Online, Offline, Setup };
static Mode mode = Mode::Connecting;
static uint32_t since = 0;
static bool started_services = false;

static DNSServer dns;
static String ap_ssid, ap_pass;
static std::vector<String> found;
static Join join = Join::Idle;
static uint32_t join_started = 0, last_retry = 0, last_visitor = 0;
static bool retry_saved_net = false;
static bool ap_failed = false;  // the setup network would not start; restart rather than show a dead QR code
static String j_ssid, j_pass, j_error;
static volatile uint8_t disconnect_reason = 0;

static const char *reason_text(uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND: return "Network not found. Is it 2.4 GHz and in range?";
    case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_AUTH_EXPIRE: case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: case WIFI_REASON_MIC_FAILURE: return "Wrong password?";
    default: return "Could not connect.";
  }
}

static void start_services() {  // once the home network is up
  if (started_services) return;
  started_services = true;
  if (MDNS.begin("w6")) MDNS.addService("http", "tcp", 80);
  configTime(0, 0, "pool.ntp.org", "time.google.com");  // UTC; main.cpp adds the location's offset
}

void begin(bool open_setup) {
  WiFi.persistent(false);  // the credentials live in our own settings, not in the Wi-Fi driver's copy
  WiFi.setHostname("w6");
  WiFi.onEvent(
      [](WiFiEvent_t, WiFiEventInfo_t info) {
        disconnect_reason = info.wifi_sta_disconnected.reason;
        bootlog::wifi_dropped(disconnect_reason);
      },
      ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  if (cfg::S.token.isEmpty()) cfg::new_token();  // the radio is on, so the random numbers are real
  if (open_setup || cfg::S.ssid.isEmpty()) {
    start_setup();
    return;
  }
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg::S.ssid.c_str(), cfg::S.pass.c_str());
  mode = Mode::Connecting, since = millis();
  ui::message(ICON_WIFI, "Connecting to", cfg::S.ssid.c_str());
}

void start_setup(bool retry_saved) {
  Serial.printf("net: opening setup%s\n", retry_saved ? " (saved network unreachable)" : "");
  retry_saved_net = retry_saved;
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();
  found.clear();
  for (int i = 0; i < n; i++) {  // results come strongest first; keep each name once
    String s = WiFi.SSID(i);
    if (s.length() && std::find(found.begin(), found.end(), s) == found.end() && found.size() < 20) found.push_back(s);
  }
  WiFi.scanDelete();
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char name[20];
  snprintf(name, sizeof name, "W6-setup-%02X%02X", mac[4], mac[5]);
  ap_ssid = name;
  ap_pass = cfg::random_string(8);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(SETUP_TX_POWER);
  bool up = false;
  for (int i = 0; i < 3 && !up; i++) {  // the QR code is only worth showing if the network really started
    up = WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str(), AP_CHANNEL) && WiFi.softAPIP() != IPAddress(0, 0, 0, 0);
    if (!up) Serial.printf("net: setup network did not start (try %d)\n", i + 1), delay(500);
  }
  WiFi.setTxPower(SETUP_TX_POWER);  // again: starting the access point can reset it
  dns.start(53, "*", WiFi.softAPIP());  // every name points at us, so phones show the setup page
  mode = Mode::Setup, since = millis(), last_retry = millis(), last_visitor = millis();
  join = Join::Idle;
  ap_failed = !up;
  if (ap_failed) {
    ui::message(ICON_WARNING, "Setup network failed", "Restarting in a minute");
    return;
  }
  ui::setup_screen(ap_ssid.c_str(), ap_pass.c_str());
  Serial.printf("net: setup network %s is up at %s\n", ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

bool online() { return WiFi.isConnected() && mode != Mode::Setup; }
bool in_setup() { return mode == Mode::Setup; }
bool setup_requested() { return mode == Mode::Setup && !retry_saved_net; }
bool from_setup_network(const IPAddress &local) { return mode == Mode::Setup && local == WiFi.softAPIP(); }
std::vector<String> networks() { return found; }
Join join_state() { return join; }
String join_error() { return j_error; }
String joined_ssid() { return j_ssid; }
String joined_pass() { return j_pass; }

void try_join(const String &ssid, const String &pass) {
  j_ssid = ssid, j_pass = pass, j_error = "";
  disconnect_reason = 0;
  WiFi.disconnect(false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  join = Join::Trying, join_started = millis();
  ui::message(ICON_WIFI, "Connecting to", ssid.c_str());
}

void loop() {
  uint32_t now = millis();
  switch (mode) {
    case Mode::Connecting:
      if (WiFi.isConnected()) {
        Serial.printf("net: online at %s (http://w6.local)\n", WiFi.localIP().toString().c_str());
        mode = Mode::Online;
        start_services();
        ui::home();
      } else if (now - since > CONNECT_MS) {
        start_setup(true);  // keeps the saved network and retries it while waiting
      }
      break;
    case Mode::Online:
      if (!WiFi.isConnected()) mode = Mode::Offline, since = now;  // the driver reconnects by itself
      break;
    case Mode::Offline:
      if (WiFi.isConnected()) mode = Mode::Online;
      break;
    case Mode::Setup:
      if (ap_failed && now - since > 60000) bootlog::restarting("setup fail"), ESP.restart();  // a fresh start usually brings the radio back
      dns.processNextRequest();
      if (WiFi.softAPgetStationNum() > 0) last_visitor = now;
      if (join == Join::Trying) {
        if (WiFi.isConnected()) {
          join = Join::Ok;
          ui::message(ICON_CHECK, "Connected", "Finish on your phone");
        } else if (now - join_started > JOIN_MS ||
                   (disconnect_reason && now - join_started > 8000 && disconnect_reason != WIFI_REASON_ASSOC_LEAVE)) {
          join = Join::Failed;
          j_error = reason_text(disconnect_reason);
          WiFi.disconnect(false);
          ui::setup_screen(ap_ssid.c_str(), ap_pass.c_str());
        }
      } else if (!retry_saved_net && join != Join::Ok && cfg::S.ssid.length() && now - last_visitor > SETUP_IDLE_MS) {
        bootlog::restarting("setup idle");
        ESP.restart();  // nobody came to set it up; back to the saved network
      } else if (retry_saved_net && join == Join::Idle && cfg::S.ssid.length() && WiFi.softAPgetStationNum() == 0 &&
                 now - last_retry > RETRY_EVERY_MS) {
        // Nobody is setting the stand up: maybe the home network was only down. Try it, then restart if it is back.
        last_retry = now;
        WiFi.begin(cfg::S.ssid.c_str(), cfg::S.pass.c_str());
        uint32_t t0 = millis();
        while (!WiFi.isConnected() && millis() - t0 < 12000) delay(100);
        if (WiFi.isConnected()) bootlog::restarting("wifi back"), ESP.restart();
        WiFi.disconnect(false);
        last_retry = millis();
      }
      break;
  }
}

}  // namespace net
