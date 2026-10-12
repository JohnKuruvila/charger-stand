// W6 stand firmware: a clock, and an HTTP API that puts progress rings and notifications on
// the round screen (README.md). The pieces:
//   ui.cpp       the screens (LVGL), also built on a desktop for previews
//   display.cpp  the GC9A01 panel under LVGL
//   net.cpp      Wi-Fi, the setup network, mDNS, network time
//   web.cpp      the setup page and the API
//   settings.cpp what is kept in flash
#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <lvgl.h>
#include <sys/time.h>

#include "bootlog.h"
#include "cpuload.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "ui.h"
#include "version.h"
#include "web.h"

RTC_NOINIT_ATTR static uint32_t crash_count;  // survives a crash or reset, not a power cut

// A freshly updated firmware that crashes three times in a row before it has run a minute is swapped back for the
// one before it, which is still in the other app slot. This matters because the USB port is sealed inside the stand.
static void boot_previous(const char *why) {
  const esp_partition_t *prev = esp_ota_get_next_update_partition(NULL);
  esp_app_desc_t desc;
  if (prev && esp_ota_get_partition_description(prev, &desc) == ESP_OK && esp_ota_set_boot_partition(prev) == ESP_OK) {
    Serial.printf("new firmware %s; going back to %s\n", why, desc.version);
    cfg::set_fresh_update(false);
    crash_count = 0;
    bootlog::restarting("rollback");
    esp_restart();
  }
}

static void roll_back_if_crashing() {
  esp_reset_reason_t r = esp_reset_reason();
  bool crashed = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
  crash_count = (crashed && r != ESP_RST_POWERON) ? crash_count + 1 : 0;
  if (crash_count < 3 || !cfg::fresh_update()) return;
  boot_previous("crashed three times");
}

// Updates only ever arrive over the home network, so new firmware that can't get back onto it within five minutes is
// broken in a way a crash check can't see (Wi-Fi, the web server). Go back to the old one rather than strand a sealed
// stand. If the router happens to be down at that moment, this costs an update, not the stand.
constexpr uint32_t FRESH_ONLINE_DEADLINE_MS = 5 * 60000;

// A loop() that stops for this long restarts the stand (start history: "watchdog") instead of leaving a frozen screen
// until someone unplugs it. The longest a healthy pass waits is about 12 s, retrying the home Wi-Fi from setup; the
// update upload, which takes longer, feeds it as it goes.
constexpr uint32_t LOOP_STALL_S = 20;

static ui::Clock local_clock() {
  ui::Clock c;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (tv.tv_sec < 1700000000 || !cfg::S.has_offset) return c;  // no network time yet, or no time zone yet
  time_t t = tv.tv_sec + cfg::S.utc_offset;
  struct tm tm;
  gmtime_r(&t, &tm);
  c.valid = true;
  c.hour = tm.tm_hour, c.minute = tm.tm_min, c.second = tm.tm_sec, c.ms = tv.tv_usec / 1000;
  c.weekday = tm.tm_wday, c.day = tm.tm_mday, c.month = tm.tm_mon + 1;
  return c;
}

// Commands over the USB serial port (only reachable with the stand open): token, status, setup, reset
static void serial_console() {
  static String line;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') {
      if (line.length() < 32) line += ch;
      continue;
    }
    line.trim();
    if (line == "token") Serial.printf("token: %s\n", cfg::S.token.c_str());
    else if (line == "status")
      Serial.printf("W6 %s, Wi-Fi %s, IP %s, free heap %u\n", W6_VERSION,
                    net::online() ? "online" : net::in_setup() ? "in setup" : "offline", WiFi.localIP().toString().c_str(),
                    ESP.getFreeHeap());
    else if (line == "setup") net::start_setup();
    else if (line == "reset") cfg::erase(), bootlog::restarting("reset"), ESP.restart();
    else if (line.length()) Serial.println("commands: token, status, setup, reset");
    line = "";
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // inside the stand nothing reads the USB port; never wait for it
  delay(300);
  bootlog::begin();
  cpuload::begin();
  roll_back_if_crashing();
  cfg::load();
  int boots = cfg::boot_count();
  Serial.printf("W6 %s starting (boot %d in a row), free heap %u\n", W6_VERSION, boots, ESP.getFreeHeap());

  if (!display::begin(cfg::rotation())) Serial.println("display: panel did not start");
  ui::init();
  ui::set_face(ui::face_named(cfg::S.face.c_str()));
  ui::set_charging_style(ui::charging_style_named(cfg::S.charging.c_str()));
  ui::set_24h(cfg::S.clock_24h);
  ui::message(ICON_SYNC, "W6", W6_VERSION);
  lv_timer_handler();

  net::begin(boots >= 3);  // plugged in three times within a few seconds each: open setup
  if (boots >= 3) cfg::clear_boot_count(), bootlog::opened_setup_by_plugging();
  web::begin();
  esp_task_wdt_init(LOOP_STALL_S, true);  // already running (5 s, panic): this only lengthens it
  enableLoopWDT();                        // from here on, so a slow start can't trip it
}

void loop() {
  uint32_t work_from = micros();
  uint32_t now = millis();
  static bool settled = false;
  if (!settled && now > 10000) {
    settled = true;
    cfg::clear_boot_count();
  }
  static bool healthy = false;
  if (!healthy && now > 60000 && net::online()) {
    healthy = true;
    crash_count = 0;
    if (cfg::fresh_update()) cfg::set_fresh_update(false);  // the new firmware is good
  }
  static bool fresh = cfg::fresh_update();
  if (fresh && !healthy && now > FRESH_ONLINE_DEADLINE_MS && !net::setup_requested()) boot_previous("never got online");

  bootlog::loop();
  net::loop();
  web::loop();
  serial_console();

  static uint32_t last_check = 0;
  if (now - last_check >= 500) {
    last_check = now;
    ui::set_wifi(net::online());
  }

  ui::tick(millis(), local_clock());  // not now: a request handled above may carry a later time
  lv_timer_handler();
  cpuload::loop_done(micros() - work_from);
  delay(2);
}
