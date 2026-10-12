#include "web.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_heap_caps.h>
#include <lvgl.h>
#include <WiFi.h>

#include "bootlog.h"
#include "cpuload.h"
#include "display.h"
#include "net.h"
#include "settings.h"
#include "ui.h"
#include "version.h"

namespace web {

static WebServer server(80);
static uint32_t restart_at = 0;  // restarts a moment after answering, so the reply gets out first
static const char *restart_why = "";

// The last now-playing update, as received (GET /api/status "media_last"): shows whether the app sends a position and
// a track length, which the progress ring needs. No titles.
static struct { uint32_t at = 0; bool playing = false; int32_t position_ms = -1, duration_ms = -1; } last_media;
static uint32_t setup_at = 0;  // opens setup a moment after answering, for the same reason

// ---- helpers
static void send_json(int code, const JsonDocument &doc) {
  String s;
  serializeJson(doc, s);
  server.send(code, "application/json", s);
}

static void send_error(int code, const String &msg) {
  JsonDocument d;
  d["error"] = msg;
  send_json(code, d);
}

static void send_ok() { server.send(200, "application/json", "{\"ok\":true}"); }

static bool token_ok() {
  const String &t = cfg::S.token;
  String h = server.header("Authorization");
  if (t.isEmpty() || !h.startsWith("Bearer ")) return false;
  String got = h.substring(7);
  got.trim();
  uint8_t diff = got.length() != t.length();
  for (size_t i = 0; i < t.length(); i++) diff |= (uint8_t)((i < got.length() ? got[i] : 0) ^ t[i]);  // same time for every token
  return diff == 0;
}

static bool authed() {
  if (token_ok()) return true;
  send_error(401, "missing or wrong token: send 'Authorization: Bearer <token>'");
  return false;
}

static bool body(JsonDocument &doc) {
  if (!server.hasArg("plain")) {  // curl -d without a content type sends a form, which WebServer splits up
    send_error(400, "send the body as JSON, with the header Content-Type: application/json");
    return false;
  }
  DeserializationError e = deserializeJson(doc, server.arg("plain"));
  if (e || !doc.is<JsonObject>()) {
    send_error(400, "body must be a JSON object");
    return false;
  }
  return true;
}

static String html_escape(const String &s) {
  String o;
  for (char c : s) {
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static const char *icon_named(const String &n) {
  if (n == "terraform" || n == "cubes") return ICON_CUBES;
  if (n == "github") return ICON_GITHUB;
  if (n == "download") return ICON_DOWNLOAD;
  if (n == "sync") return ICON_SYNC;
  if (n == "info") return ICON_INFO;
  if (n == "warning") return ICON_WARNING;
  return ICON_TERMINAL;
}

// ---- setup page, only over the setup network
static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>W6 setup</title>
<style>
:root{color-scheme:light dark}
body{font:16px/1.5 system-ui,sans-serif;max-width:28rem;margin:0 auto;padding:1rem}
label{display:block;margin-top:1rem;font-weight:600}
input,select,button{font:inherit;width:100%;box-sizing:border-box;min-height:2.75rem;margin-top:.25rem;padding:.4rem .6rem}
input[type=checkbox]{width:auto;min-height:0;margin-right:.5rem}
.opt{font-weight:400;display:flex;align-items:flex-start;margin-top:.75rem}
.opt input{margin-top:.35rem}
button{margin-top:1.5rem;font-weight:600}
.hint{font-size:.875rem;opacity:.8}
code{font-size:1rem;word-break:break-all;background:rgba(127,127,127,.18);padding:.1rem .3rem;border-radius:.25rem}
.err{color:#d33}
</style></head><body>
<h1>Set up W6</h1>
<form id=f>
<label for=ssid>Wi-Fi network</label>
<select id=ssid name=ssid>%NETWORKS%<option value="">Other network&hellip;</option></select>
<input id=other name=other placeholder="Network name" aria-label="Network name" hidden>
<label for=pass>Password</label>
<input id=pass name=pass type=password autocomplete=current-password>
<input id=tz name=tz type=hidden>
<label class=opt><input type=checkbox name=newtoken>Make a new API token (scripts using the old one stop working)</label>
<p class=hint>W6 can only use 2.4 GHz Wi-Fi. The clock takes this phone's time zone.</p>
<button>Connect</button>
</form>
<div id=out></div>
<button id=fin type=button hidden>Finish</button>
<script>
const $=id=>document.getElementById(id);
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const show=h=>{$('out').innerHTML=h};
let timer=null;
const later=()=>{if(!timer)timer=setTimeout(()=>{timer=null;poll()},1000)};
const other=()=>{$('other').hidden=$('ssid').value!==''};
$('ssid').onchange=other;other();
$('tz').value=-new Date().getTimezoneOffset()*60;
$('f').onsubmit=async e=>{
  e.preventDefault();$('f').hidden=true;show('<p>Connecting&hellip;</p>');
  await fetch('/setup/join',{method:'POST',body:new URLSearchParams(new FormData($('f')))});poll();
};
async function poll(){
  let s;
  try{s=await(await fetch('/setup/state')).json()}catch(e){later();return}
  if(s.join==='trying'){show('<p>Connecting to <b>'+esc(s.ssid)+'</b>&hellip;</p>');later();return}
  if(s.join==='failed'){show('<p class=err>'+esc(s.error)+'</p><button onclick="location.reload()">Try again</button>');return}
  show('<p>Connected to <b>'+esc(s.ssid)+'</b>.</p>');
  $('fin').hidden=false;
}
$('fin').onclick=async()=>{
  const r=await(await fetch('/setup/finish',{method:'POST'})).json();
  $('fin').hidden=true;
  show('<p>W6 is set up and restarting.</p>'+
    '<p>Address: <code>http://w6.local</code> or <code>http://'+esc(r.ip)+'</code></p>'+
    '<p>API token:</p><p><code>'+esc(r.token)+'</code></p>'+
    '<p class=hint>Keep the token like a password. To see it again, open setup again: plug the stand in and unplug it '+
    'within five seconds, twice, then plug it in to stay.</p>');
};
</script></body></html>)HTML";

static bool pending_newtoken = false;
static String pending_tz;  // the setup phone's offset from UTC in seconds, as its browser reports it

static void setup_page() {
  String opts;
  for (const String &s : net::networks()) {
    String e = html_escape(s);
    opts += "<option value=\"" + e + "\"" + (s == cfg::S.ssid ? " selected" : "") + ">" + e + "</option>";
  }
  String page = FPSTR(SETUP_HTML);
  page.replace("%NETWORKS%", opts);
  server.send(200, "text/html", page);
}

static void setup_join() {
  String ssid = server.arg("ssid").length() ? server.arg("ssid") : server.arg("other");
  if (ssid.isEmpty() || ssid.length() > 32 || server.arg("pass").length() > 63) return send_error(400, "bad network name or password");
  pending_newtoken = server.hasArg("newtoken");
  pending_tz = server.arg("tz");
  net::try_join(ssid, server.arg("pass"));
  send_ok();
}

static void setup_state() {
  JsonDocument d;
  net::Join j = net::join_state();
  d["join"] = j == net::Join::Trying ? "trying" : j == net::Join::Ok ? "ok" : j == net::Join::Failed ? "failed" : "idle";
  d["ssid"] = net::joined_ssid();
  d["error"] = net::join_error();
  send_json(200, d);
}

// A UTC offset in seconds, if it is one a real time zone uses (-12:00 to +14:00, in whole minutes)
static bool valid_offset(long s) { return s >= -12 * 3600 && s <= 14 * 3600 && s % 60 == 0; }

static void setup_finish() {
  if (net::join_state() != net::Join::Ok) return send_error(409, "not connected yet");
  cfg::S.ssid = net::joined_ssid();
  cfg::S.pass = net::joined_pass();
  char *end;
  long tz = strtol(pending_tz.c_str(), &end, 10);
  if (pending_tz.length() && !*end && valid_offset(tz)) cfg::S.utc_offset = tz, cfg::S.has_offset = true;
  if (pending_newtoken) cfg::S.token = cfg::random_string(24);
  cfg::save();
  JsonDocument d;
  d["ip"] = WiFi.localIP().toString();
  d["token"] = cfg::S.token;
  send_json(200, d);
  ui::message(ICON_CHECK, "Set up", "Restarting");
  restart_at = millis() + 1500, restart_why = "set up";
}

// ---- normal pages and the API
static void index_page() {
  if (net::from_setup_network(server.client().localIP())) return setup_page();
  String s = F("<!doctype html><html lang=en><head><meta charset=utf-8>"
               "<meta name=viewport content=\"width=device-width,initial-scale=1\"><title>W6</title>"
               "<style>body{font:16px/1.5 system-ui,sans-serif;max-width:36rem;margin:2rem auto;padding:0 1rem}"
               "code{background:rgba(127,127,127,.18);padding:.1rem .3rem}</style></head><body><h1>W6</h1>"
               "<p>Firmware ");
  s += W6_VERSION;
  s += F(". The API is under <code>/api/</code> and needs the token from setup, sent as "
         "<code>Authorization: Bearer &lt;token&gt;</code>. See README.md in the source for the endpoints.</p>"
         "</body></html>");
  server.send(200, "text/html", s);
}

// Request timings by route (GET /api/status "http"): how often each is called and how long the stand takes, reply
// included. Nothing else runs while a request is handled, so the screen stands still for that long.
struct Route { String name; uint32_t n = 0, total_ms = 0, max_ms = 0, upload_ms = 0; };
static Route routes[24];
static int nroutes = 0;

static int route(const char *uri, HTTPMethod m) {
  if (nroutes == 24) return -1;
  routes[nroutes].name = String(m == HTTP_ANY ? "ANY" : http_method_str(m)) + " " + uri;
  return nroutes++;
}

static void count(int r, uint32_t t0) {
  if (r < 0) return;
  Route &x = routes[r];
  uint32_t ms = (micros() - t0) / 1000 + x.upload_ms;
  x.n++, x.total_ms += ms, x.upload_ms = 0;
  if (ms > x.max_ms) x.max_ms = ms;
}

static void on(const char *uri, HTTPMethod m, void (*fn)()) {
  int r = route(uri, m);
  server.on(uri, m, [r, fn] {
    uint32_t t0 = micros();
    fn();
    count(r, t0);
  });
}

static void on(const char *uri, HTTPMethod m, void (*done)(), void (*upload)()) {  // a file upload, in pieces
  int r = route(uri, m);
  server.on(
      uri, m,
      [r, done] {
        uint32_t t0 = micros();
        done();
        count(r, t0);
      },
      [r, upload] {
        uint32_t t0 = micros();
        upload();
        if (r >= 0) routes[r].upload_ms += (micros() - t0) / 1000;
      });
}

static void api_status() {
  if (!authed()) return;
  JsonDocument d;
  d["version"] = W6_VERSION;
  d["uptime_s"] = millis() / 1000;
  d["reset_reason"] = bootlog::reset_text(esp_reset_reason());
  d["ip"] = WiFi.localIP().toString();
  d["rssi_dbm"] = WiFi.RSSI();
  d["free_heap"] = ESP.getFreeHeap();
  d["min_free_heap"] = ESP.getMinFreeHeap();  // since this start
  d["largest_free_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);  // the biggest single allocation possible
  cpuload::to_json(d["cpu"].to<JsonObject>());
  d["rotation"] = cfg::rotation();
  d["face"] = cfg::S.face;
  JsonArray faces = d["faces"].to<JsonArray>();  // every face this firmware has, for the app's picker
  for (int i = 0; i < (int)ui::Face::Count; i++) faces.add(ui::face_name((ui::Face)i));
  d["charging_style"] = cfg::S.charging;
  JsonArray styles = d["charging_styles"].to<JsonArray>();  // how the charging screen can look, for the app's picker
  for (int i = 0; i < (int)ui::ChargingStyle::Count; i++) styles.add(ui::charging_style_name((ui::ChargingStyle)i));
  d["clock_24h"] = cfg::S.clock_24h;
  if (cfg::S.has_offset) d["utc_offset_s"] = cfg::S.utc_offset;
  d["living_frames"] = ui::living_frames();  // frames drawn by living faces and Warp/Swarm since the start, for their frame rate  // the clock's time zone; absent until one is set
  if (last_media.at) {
    JsonObject lm = d["media_last"].to<JsonObject>();
    lm["age_s"] = (millis() - last_media.at) / 1000;
    lm["playing"] = last_media.playing;
    lm["position_ms"] = last_media.position_ms, lm["duration_ms"] = last_media.duration_ms;  // -1: not sent
  }
  d["chip_temp_c"] = roundf(temperatureRead() * 10) / 10;  // the ESP32's own sensor: a rough guide to heat in the base
  JsonArray feat = d["features"].to<JsonArray>();  // what this firmware can do, so the app can hide the rest
  for (const char *f : {"phone", "timer", "faces", "rotation", "log", "media", "pair", "cpu", "fun", "clock_24h", "screenshot_rle", "http", "utc_offset", "charging_styles"}) feat.add(f);
  int32_t left = ui::timer_left_s(millis());
  if (left >= 0) d["timer_left_s"] = left;
  lv_mem_monitor_t lm;  // LVGL's own pool for screens and objects (LV_MEM_SIZE): if it fills, drawing fails
  lv_mem_monitor(&lm);
  JsonObject lvm = d["lvgl_mem"].to<JsonObject>();
  lvm["free"] = lm.free_size, lvm["biggest_free"] = lm.free_biggest_size, lvm["used_pct"] = lm.used_pct;
  lvm["frag_pct"] = lm.frag_pct, lvm["max_used"] = lm.max_used;
  JsonObject http = d["http"].to<JsonObject>();
  for (int i = 0; i < nroutes; i++) {
    if (!routes[i].n) continue;
    JsonObject o = http[routes[i].name].to<JsonObject>();
    o["n"] = routes[i].n, o["avg_ms"] = routes[i].total_ms / routes[i].n, o["max_ms"] = routes[i].max_ms;
  }
  send_json(200, d);
}

static void api_log() {  // the last 20 starts and this start's Wi-Fi drops (bootlog.h)
  if (!authed()) return;
  JsonDocument d;
  bootlog::to_json(d.to<JsonObject>());
  send_json(200, d);
}

static void api_progress() {
  JsonDocument d;
  if (!authed() || !body(d)) return;
  static char title[64], detail[96];
  strlcpy(title, d["title"] | "", sizeof title);
  strlcpy(detail, d["detail"] | "", sizeof detail);
  String st = d["state"] | "running";
  ui::Progress p;
  if (st == "running") p.state = ui::State::Running;
  else if (st == "success") p.state = ui::State::Success;
  else if (st == "failed") p.state = ui::State::Failed;
  else return send_error(400, "state must be running, success or failed");
  JsonVariant pc = d["percent"];
  if (!pc.isNull() && !pc.is<float>()) return send_error(400, "percent must be a number 0-100, or null");
  p.percent = pc.isNull() ? -1 : constrain((int)lroundf(pc.as<float>()), 0, 100);
  p.title = title, p.detail = detail;
  p.icon = icon_named(d["icon"] | "terminal");
  uint32_t ttl = d["ttl"] | (p.state == ui::State::Running ? 600 : 60);
  p.ttl_s = constrain(ttl, 5u, 86400u);
  ui::progress(p, millis());
  send_ok();
}

// Reads a number sent as 86, 86.0 or "86" (automation apps often send everything as text); NAN if it isn't one.
// Double, not float: a float holds a Unix time only to the nearest 128 s.
static double number(JsonVariant v) {
  if (v.is<double>()) return v.as<double>();
  if (v.is<const char *>()) {
    const char *s = v.as<const char *>();
    char *end;
    double f = strtod(s, &end);
    if (end != s) return f;
  }
  return NAN;
}

// {"percent": 86, "charging": true, "watts": 2.8, "ttl": 900}: the phone's battery, from an automation app on it
static void api_phone() {
  JsonDocument d;
  if (!authed() || !body(d)) return;
  float pc = number(d["percent"]);
  if (isnan(pc)) return send_error(400, "percent (0-100) is required");
  ui::Phone p;
  p.percent = constrain((int)lroundf(pc), 0, 100);
  JsonVariant ch = d["charging"];
  if (ch.is<bool>()) p.charging = ch.as<bool>();
  else if (ch.is<const char *>()) {
    String s = ch.as<String>();
    s.toLowerCase();
    p.charging = s == "true" || s == "1" || s == "yes" || s == "on" || s == "charging" || s == "full";
  } else if (!isnan(number(ch))) p.charging = number(ch) != 0;
  else p.charging = true;  // most automations only fire while it charges
  float w = number(d["watts"]);
  p.watts = isnan(w) ? -1 : fabsf(w);  // some phones report charging as negative power
  float ttl = number(d["ttl"]);
  ui::phone(p, isnan(ttl) ? 900 : constrain((uint32_t)ttl, 10u, 86400u), millis());
  send_ok();
}

// {"minutes": 25, "label": "Focus"} or {"seconds": 90}: a countdown on the screen
static void api_timer() {
  JsonDocument d;
  if (!authed() || !body(d)) return;
  float s = number(d["seconds"]), m = number(d["minutes"]);
  float total = !isnan(s) ? s : !isnan(m) ? m * 60 : NAN;
  if (isnan(total) || total < 1 || total > 86400)
    return send_error(400, "send {\"minutes\": 25} or {\"seconds\": 90} (up to 24 h), and optionally \"label\"");
  const char *label = d["label"] | "Timer";
  ui::timer(label, (uint32_t)lroundf(total), millis());
  send_ok();
}

// Now playing, from the W6 app. Album art goes separately (api_art) and only when it changes: the reply says whether
// the stand already has the art named by art_id.
static String art_id;  // the art on screen now; empty for none
static String art_upload_id, art_error;
static size_t art_got = 0;
static bool art_authed = false;

static void api_media() {
  JsonDocument d;
  if (!authed() || !body(d)) return;
  ui::Media m;
  m.title = d["title"] | "";
  m.artist = d["artist"] | "";
  m.playing = d["playing"] | true;
  double pos = number(d["position_ms"]), dur = number(d["duration_ms"]);
  m.position_ms = isnan(pos) || pos < 0 ? -1 : (int32_t)pos;
  m.duration_ms = isnan(dur) || dur <= 0 ? -1 : (int32_t)dur;
  float ttl = number(d["ttl"]);
  ui::media(m, isnan(ttl) ? 300 : constrain((uint32_t)ttl, 10u, 86400u), millis());
  last_media.at = millis() | 1, last_media.playing = m.playing;
  last_media.position_ms = m.position_ms, last_media.duration_ms = m.duration_ms;
  String want = d["art_id"] | "";
  if (want.isEmpty() && art_id.length()) art_id = "", ui::art_ready(false);
  JsonDocument r;
  r["ok"] = true;
  r["art"] = want.isEmpty() ? "none" : want == art_id && ui::art_loaded() ? "have" : "need";
  send_json(200, r);
}

static void api_media_clear() {
  if (!authed()) return;
  ui::clear_media();
  send_ok();
}

// POST /api/media/art?id=<art_id>, multipart form with one file of ui::ART_BYTES (RGB565, high byte first)
static void art_upload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    art_authed = token_ok(), art_got = 0, art_error = "";
    art_upload_id = server.arg("id");
    // the art buffer is 25 KB; leave room for the web server and Wi-Fi on top of it
    if (art_authed && !ui::art_loaded() && ESP.getFreeHeap() < (uint32_t)ui::ART_BYTES + 30000) art_error = "not enough memory for album art right now";
    else if (art_authed && !ui::art_buffer()) art_error = "not enough memory for album art";
  } else if (!art_authed || art_error.length()) {
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (art_got + up.currentSize > (size_t)ui::ART_BYTES) art_error = "art must be exactly " + String(ui::ART_BYTES) + " bytes";
    else memcpy(ui::art_buffer() + art_got, up.buf, up.currentSize), art_got += up.currentSize;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    art_error = "upload aborted";
  }
}

static void art_done() {
  if (!art_authed) return send_error(401, "missing or wrong token");
  if (art_error.isEmpty() && art_got != (size_t)ui::ART_BYTES)
    art_error = "art must be exactly " + String(ui::ART_BYTES) + " bytes";
  if (art_error.length()) {
    art_id = "";
    ui::art_ready(false);
    return send_error(art_error.startsWith("not enough") ? 503 : 400, art_error);
  }
  art_id = art_upload_id.length() ? art_upload_id : String("art");
  ui::art_ready(true);
  send_ok();
}

// Pairing the app without typing the token. POST /api/pair (no token) shows a one-time code on the screen, as a QR
// code and as text, for two minutes; POST /api/pair/finish with that code returns the token. Only someone who can see
// the screen can pair. Codes are 8 characters from 31 (about 10^12), five wrong tries end the code, and a new code
// can be started only every 30 seconds.
constexpr uint32_t PAIR_MS = 120000, PAIR_GAP_MS = 30000;
constexpr int PAIR_TRIES = 5;
static String pair_code;
static uint32_t pair_until = 0, pair_started = 0;
static int pair_tries = 0;

static void not_found();

static bool pairing() { return pair_code.length() && (int32_t)(millis() - pair_until) < 0; }

static void end_pairing() {
  pair_code = "";
  ui::clear_pair();
}

static void api_pair() {
  if (net::from_setup_network(server.client().localIP())) return not_found();
  if (cfg::S.token.isEmpty()) return send_error(409, "the stand isn't set up yet");
  if (!pairing()) {
    if (pair_started && millis() - pair_started < PAIR_GAP_MS) return send_error(429, "a code was shown moments ago; try again in a few seconds");
    pair_code = cfg::random_string(8);
    pair_tries = 0, pair_started = millis(), pair_until = millis() + PAIR_MS;
    String payload = "w6://pair?host=" + WiFi.localIP().toString() + "&code=" + pair_code;
    String shown = pair_code.substring(0, 4) + " " + pair_code.substring(4);
    shown.toUpperCase();
    ui::pair_screen(payload.c_str(), shown.c_str(), PAIR_MS / 1000, millis());
  }
  JsonDocument r;
  r["ok"] = true;
  r["expires_s"] = (pair_until - millis()) / 1000;
  send_json(200, r);
}

static void api_pair_finish() {
  JsonDocument d;
  if (net::from_setup_network(server.client().localIP())) return not_found();
  if (!body(d)) return;
  if (!pairing()) return send_error(410, "no code is showing on the stand; start pairing again");
  String got = d["code"] | "";
  got.toLowerCase();
  got.replace(" ", "");
  got.replace("-", "");
  uint8_t diff = got.length() != pair_code.length();
  for (size_t i = 0; i < pair_code.length(); i++) diff |= (uint8_t)((i < got.length() ? got[i] : 0) ^ pair_code[i]);
  if (diff) {
    if (++pair_tries >= PAIR_TRIES) {
      end_pairing();
      return send_error(403, "too many wrong codes; start pairing again");
    }
    return send_error(403, "wrong code");
  }
  end_pairing();
  ui::notify("Paired with the W6 app", ui::Level::Success, 5, millis());
  JsonDocument r;
  r["token"] = cfg::S.token;
  send_json(200, r);
}

static void api_timer_clear() {
  if (!authed()) return;
  ui::clear_timer();
  send_ok();
}

// Plays a show now: {"show": "420"}, the default, also with no body.
static void api_fun() {
  if (!authed()) return;
  JsonDocument d;
  if (server.hasArg("plain") && server.arg("plain").length()) deserializeJson(d, server.arg("plain"));
  const char *show = d["show"] | "420";
  if (!ui::show(show, millis())) return send_error(400, "no such show");
  send_ok();
}

static void api_phone_clear() {
  if (!authed()) return;
  ui::clear_phone();
  send_ok();
}

static void api_progress_clear() {
  if (!authed()) return;
  ui::clear_progress();
  send_ok();
}

static void api_notify() {
  JsonDocument d;
  if (!authed() || !body(d)) return;
  String text = d["text"] | "";
  if (text.isEmpty()) return send_error(400, "text is required");
  String lv = d["level"] | "info";
  ui::Level level = lv == "success" ? ui::Level::Success
                    : lv == "warning" ? ui::Level::Warning
                    : lv == "error" ? ui::Level::Error
                    : ui::Level::Info;
  uint32_t secs = d["seconds"] | 10;
  ui::notify(text.c_str(), level, constrain(secs, 1u, 3600u), millis());
  send_ok();
}

static void api_settings() {
  JsonDocument d;
  if (!authed() || !body(d)) return;
  JsonDocument r;
  if (d["face"].is<const char *>()) {  // {"face": "classic"}: one of "faces" in GET /api/status
    String f = d["face"].as<String>();
    f.toLowerCase();
    if (!ui::face_exists(f.c_str())) return send_error(400, "unknown face; GET /api/status lists them under \"faces\"");
    cfg::S.face = f;
    cfg::save();
    ui::set_face(ui::face_named(f.c_str()));
    r["face"] = f;
    return send_json(200, r);
  }
  if (d["charging_style"].is<const char *>()) {  // {"charging_style": "warp"}: one of "charging_styles" in GET /api/status
    String s = d["charging_style"].as<String>();
    s.toLowerCase();
    if (!ui::charging_style_exists(s.c_str()))
      return send_error(400, "unknown charging_style; GET /api/status lists them under \"charging_styles\"");
    cfg::S.charging = s;
    cfg::save();
    ui::set_charging_style(ui::charging_style_named(s.c_str()));
    r["charging_style"] = s;
    return send_json(200, r);
  }
  if (d["clock_24h"].is<bool>()) {  // {"clock_24h": false} for 4:20 PM (the default), true for 16:20
    cfg::S.clock_24h = d["clock_24h"];
    cfg::save();
    ui::set_24h(cfg::S.clock_24h);
    r["clock_24h"] = cfg::S.clock_24h;
    return send_json(200, r);
  }
  if (d["utc_offset_s"].is<long>()) {  // {"utc_offset_s": 19800}: the clock's time zone, seconds east of UTC (the app
    long o = d["utc_offset_s"];       // sends the phone's)
    if (!valid_offset(o)) return send_error(400, "utc_offset_s is -43200 to 50400, in whole minutes");
    cfg::S.utc_offset = o, cfg::S.has_offset = true;
    cfg::save();
    r["utc_offset_s"] = o;
    return send_json(200, r);
  }
  if (d["rotation"].is<int>()) {  // {"rotation": 0-3}: quarter turns clockwise; kept across a reset
    int q = d["rotation"];
    if (q < 0 || q > 3) return send_error(400, "rotation is 0, 1, 2 or 3 (quarter turns clockwise)");
    cfg::set_rotation(q);
    display::set_rotation(q);
    r["rotation"] = q;
    return send_json(200, r);
  }
  send_error(400, "send one of face, charging_style, clock_24h, utc_offset_s or rotation");
}

// GET /api/screenshot: a BMP, or with ?format=rle the same pixels run-length coded (W6R1, README), about a tenth of
// the size. The screen stands still while it is sent, so the app asks for the small one.
struct ChunkOut : Print {  // sends what it is given as one chunk of a chunked reply; nothing once the client has gone
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t *b, size_t n) override {
    if (!server.client().connected()) return 0;
    return server.sendContent((const char *)b, n), n;
  }
};

static void api_screenshot() {
  if (!authed()) return;
  bool sent;
  if (server.arg("format") == "rle") {
    ChunkOut out;
    sent = display::screenshot(out, true, [] {
      server.setContentLength(CONTENT_LENGTH_UNKNOWN);
      server.send(200, "application/octet-stream", "");
    });
    if (sent) server.sendContent("", 0);  // the last, empty chunk
  } else {
    WiFiClient c = server.client();
    sent = display::screenshot(c, false, [] {
      server.setContentLength(14 + 40 + 12 + 240 * 240 * 2);
      server.send(200, "image/bmp", "");
    });
  }
  if (!sent) send_error(503, "not enough memory for a screenshot; try again");
}

// Firmware upload: curl -H "Authorization: Bearer $W6_TOKEN" -F firmware=@firmware.bin http://w6.local/api/ota
static String ota_error;
static bool ota_authed = false;
static uint32_t ota_size = 0;
static int ota_shown = -1;

static void ota_progress(int pct, ui::State st, const char *detail) {
  ui::Progress p;
  p.title = "Firmware update", p.percent = pct, p.state = st, p.detail = detail, p.icon = ICON_DOWNLOAD, p.ttl_s = 120;
  ui::progress(p, millis());
  ui::tick(millis(), ui::Clock());
  lv_timer_handler();
}

static void ota_upload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    ota_error = "", ota_shown = -1;
    ota_authed = token_ok();
    if (!ota_authed) return;
    ota_size = server.header("Content-Length").toInt();  // the whole form, a little more than the file
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) ota_error = Update.errorString();
    Serial.printf("ota: receiving %s (%u bytes)\n", up.filename.c_str(), (unsigned)ota_size);
  } else if (!ota_authed || ota_error.length()) {
    if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) Update.abort();
    return;
  } else if (up.status == UPLOAD_FILE_WRITE) {
    feedLoopWDT();  // the whole upload is one pass of loop()
    if (Update.write(up.buf, up.currentSize) != up.currentSize) ota_error = Update.errorString();
    int pct = ota_size ? min<int>(99, (uint64_t)up.totalSize * 100 / ota_size) : -1;
    if (pct / 5 != ota_shown / 5) ota_shown = pct, ota_progress(pct, ui::State::Running, up.filename.c_str());
  } else if (up.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) ota_error = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    ota_error = "upload aborted";
  }
}

static void ota_done() {
  if (!ota_authed) return send_error(401, "missing or wrong token");
  if (ota_error.length()) {
    Serial.printf("ota: failed: %s\n", ota_error.c_str());
    ota_progress(ota_shown, ui::State::Failed, ota_error.c_str());
    return send_error(500, ota_error);
  }
  Serial.println("ota: done, restarting into the new firmware");
  cfg::set_fresh_update(true);
  ota_progress(100, ui::State::Success, "Restarting");
  send_ok();
  restart_at = millis() + 1000, restart_why = "update";
}

static void not_found() {
  if (net::from_setup_network(server.client().localIP())) {  // phones probing for a captive portal
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    server.send(302, "text/plain", "");
    return;
  }
  send_error(404, "no such endpoint");
}

void begin() {
  static const char *headers[] = {"Authorization", "Content-Length"};
  server.collectHeaders(headers, 2);
  server.on("/", HTTP_GET, index_page);
  server.on("/setup/join", HTTP_POST, [] { net::from_setup_network(server.client().localIP()) ? setup_join() : not_found(); });
  server.on("/setup/state", HTTP_GET, [] { net::from_setup_network(server.client().localIP()) ? setup_state() : not_found(); });
  server.on("/setup/finish", HTTP_POST, [] { net::from_setup_network(server.client().localIP()) ? setup_finish() : not_found(); });
  on("/api/status", HTTP_GET, api_status);
  on("/api/log", HTTP_GET, api_log);
  on("/api/progress", HTTP_POST, api_progress);
  on("/api/progress", HTTP_DELETE, api_progress_clear);
  on("/api/notify", HTTP_POST, api_notify);
  on("/api/phone", HTTP_POST, api_phone);
  on("/api/phone", HTTP_DELETE, api_phone_clear);
  on("/api/timer", HTTP_POST, api_timer);
  on("/api/timer", HTTP_DELETE, api_timer_clear);
  on("/api/fun", HTTP_POST, api_fun);
  on("/api/media", HTTP_POST, api_media);
  on("/api/media", HTTP_DELETE, api_media_clear);
  on("/api/media/art", HTTP_POST, art_done, art_upload);
  on("/api/pair", HTTP_POST, api_pair);
  on("/api/pair/finish", HTTP_POST, api_pair_finish);
  on("/api/settings", HTTP_POST, api_settings);
  on("/api/screenshot", HTTP_GET, api_screenshot);
  on("/api/ota", HTTP_POST, ota_done, ota_upload);
  server.on("/api/reboot", HTTP_POST, [] {
    if (!authed()) return;
    send_ok();
    restart_at = millis() + 500, restart_why = "reboot";
  });
  server.on("/api/setup", HTTP_POST, [] {  // opens the setup network; the stand leaves the home network
    if (!authed()) return;
    send_ok();
    setup_at = millis() + 500;
  });
  server.on("/api/reset", HTTP_POST, [] {  // forgets Wi-Fi, location and token, then restarts into setup
    if (!authed()) return;
    send_ok();
    cfg::erase();
    restart_at = millis() + 500, restart_why = "reset";
  });
  server.onNotFound(not_found);
  server.begin();
}

void loop() {
  server.handleClient();
  if (restart_at && (int32_t)(millis() - restart_at) >= 0) bootlog::restarting(restart_why), ESP.restart();
  if (setup_at && (int32_t)(millis() - setup_at) >= 0) setup_at = 0, net::start_setup();
}

}  // namespace web
