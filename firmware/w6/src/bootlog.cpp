#include "bootlog.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <sys/time.h>

namespace bootlog {

constexpr int KEEP = 20;
constexpr uint32_t SAVE_EVERY_S = 600;  // plus at 10 s and 60 s, so short runs in a loop are measured closely

enum : uint8_t { ONLINE = 1, SETUP_PLUG = 2, TIMED = 4, EXACT = 8 };

struct Rec {
  uint32_t seq;      // start number since the log began; 0 = empty slot
  uint32_t started;  // Unix time of the start; 0 until network time arrives
  uint32_t ran_s;    // how long the run lasted, as last saved (a lower bound unless EXACT)
  uint32_t min_heap;
  uint16_t drops;    // Wi-Fi drops during the run, after it first got online
  uint8_t reason;    // esp_reset_reason_t that began the run
  uint8_t flags;
  int8_t max_temp;   // chip sensor, °C
  char why[11];      // why the firmware restarted itself at the end of the run, if it did
};

// RAM that survives a crash, watchdog or software restart (not a power cut), so the run before one of those can be
// measured to the second and the reason for a deliberate restart is kept.
struct Live {
  uint32_t magic, seq, up_s;
  char why[11];
};
RTC_NOINIT_ATTR static Live live;
constexpr uint32_t MAGIC = 0x57364C47;

static Rec recs[KEEP];
static Rec *cur = nullptr;
static Preferences prefs;
static uint32_t saved_at = 0;

struct Drop {
  uint32_t up_s;
  uint8_t reason;
  bool joining;  // before the first connection of this start: failed attempts, not drops
};
static Drop drops[16];
static volatile uint16_t drop_count = 0, real_drops = 0;
static volatile bool was_online = false;
static portMUX_TYPE drop_lock = portMUX_INITIALIZER_UNLOCKED;

static void save() {
  if (!prefs.begin("w6log", false)) return;
  prefs.putBytes("recs", recs, sizeof recs);
  prefs.end();
  saved_at = cur ? cur->ran_s : 0;
}

void begin() {
  if (prefs.begin("w6log", true)) {
    if (prefs.getBytesLength("recs") == sizeof recs) prefs.getBytes("recs", recs, sizeof recs);
    prefs.end();
  }
  uint32_t last = 0;
  for (auto &r : recs) last = max(last, r.seq);
  esp_reset_reason_t reason = esp_reset_reason();
  Rec *prev = last ? &recs[(last - 1) % KEEP] : nullptr;
  if (prev && prev->seq == last && reason != ESP_RST_POWERON && live.magic == MAGIC && live.seq == last) {
    prev->ran_s = max(prev->ran_s, live.up_s);
    prev->flags |= EXACT;
    memcpy(prev->why, live.why, sizeof prev->why);
    prev->why[sizeof prev->why - 1] = 0;
  }
  cur = &recs[last % KEEP];
  *cur = Rec{};
  cur->seq = last + 1;
  cur->reason = reason;
  cur->min_heap = ESP.getFreeHeap();
  cur->max_temp = INT8_MIN;
  live = Live{MAGIC, cur->seq, 0, ""};
  save();
}

void opened_setup_by_plugging() {
  if (!cur) return;
  cur->flags |= SETUP_PLUG;
  save();
}

void loop() {
  if (!cur) return;
  uint32_t up = millis() / 1000;
  if (up == live.up_s) return;  // the rest runs about once a second
  live.up_s = cur->ran_s = up;
  cur->drops = real_drops;
  cur->min_heap = ESP.getMinFreeHeap();
  bool changed = false;
  if (up % 30 == 0 || cur->max_temp == INT8_MIN) {
    int t = lroundf(temperatureRead());
    if (t > cur->max_temp) cur->max_temp = constrain(t, -127, 127);
  }
  if (!(cur->flags & ONLINE) && WiFi.isConnected()) cur->flags |= ONLINE, was_online = true, changed = true;
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  if (!(cur->flags & TIMED) && tv.tv_sec > 1700000000) {
    cur->started = tv.tv_sec - millis() / 1000;
    cur->flags |= TIMED, changed = true;
  }
  bool milestone = (up == 10 || up == 60) && saved_at < up;
  if (changed || milestone || up - saved_at >= SAVE_EVERY_S) save();
}

void restarting(const char *why) {
  strlcpy(live.why, why, sizeof live.why);
  if (!cur) return;
  strlcpy(cur->why, why, sizeof cur->why);
  cur->ran_s = live.up_s = millis() / 1000;
  save();
}

void wifi_dropped(uint8_t reason) {
  portENTER_CRITICAL(&drop_lock);
  drops[drop_count % 16] = Drop{millis() / 1000, reason, !was_online};
  drop_count = drop_count + 1;
  if (was_online) real_drops = real_drops + 1;
  portEXIT_CRITICAL(&drop_lock);
}

const char *reset_text(int reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_SW: return "restart";
    case ESP_RST_PANIC: return "crash";
    case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "other";
  }
}

static const char *drop_text(uint8_t r) {
  switch (r) {
    case WIFI_REASON_BEACON_TIMEOUT: return "router went quiet";
    case WIFI_REASON_NO_AP_FOUND: return "network not found";
    case WIFI_REASON_AUTH_FAIL: case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: case WIFI_REASON_HANDSHAKE_TIMEOUT:
      return "password handshake failed";
    case WIFI_REASON_AUTH_EXPIRE: case WIFI_REASON_ASSOC_EXPIRE: case WIFI_REASON_TIMEOUT: return "timed out";
    case WIFI_REASON_ASSOC_LEAVE: return "left on purpose";
    case WIFI_REASON_ASSOC_FAIL: case WIFI_REASON_CONNECTION_FAIL: return "connection failed";
    default: return "other";
  }
}

void to_json(JsonObject out) {
  JsonArray boots = out["boots"].to<JsonArray>();  // newest first
  uint32_t last = cur ? cur->seq : 0;
  for (uint32_t s = last; s > 0 && s + KEEP > last; s--) {
    const Rec &r = recs[(s - 1) % KEEP];
    if (r.seq != s) break;
    JsonObject b = boots.add<JsonObject>();
    b["n"] = r.seq;
    if (r.flags & TIMED) b["started"] = r.started;  // Unix time
    else b["started"] = (const char *)nullptr;          // never got network time
    b["reason"] = reset_text(r.reason);
    b["ran_s"] = r.ran_s;
    b["ran_exact"] = &r == cur || (r.flags & EXACT);  // otherwise "at least": a power cut stops the clock unseen
    b["running"] = &r == cur;
    b["got_online"] = (bool)(r.flags & ONLINE);
    b["setup_by_plugging"] = (bool)(r.flags & SETUP_PLUG);
    b["wifi_drops"] = r.drops;
    if (r.max_temp != INT8_MIN) b["max_chip_c"] = r.max_temp;
    b["min_free_heap"] = r.min_heap;
    if (r.why[0]) b["restarted_for"] = r.why;
  }
  JsonArray d = out["wifi_drops"].to<JsonArray>();  // since this start, oldest first
  portENTER_CRITICAL(&drop_lock);
  uint16_t n = drop_count;
  Drop copy[16];
  memcpy(copy, drops, sizeof copy);
  portEXIT_CRITICAL(&drop_lock);
  for (uint16_t i = n > 16 ? n - 16 : 0; i < n; i++) {
    JsonObject e = d.add<JsonObject>();
    e["uptime_s"] = copy[i % 16].up_s;
    e["code"] = copy[i % 16].reason;
    e["reason"] = drop_text(copy[i % 16].reason);
    e["while_joining"] = copy[i % 16].joining;
  }
  out["wifi_drops_total"] = n;  // including failed attempts while joining
}

}  // namespace bootlog
