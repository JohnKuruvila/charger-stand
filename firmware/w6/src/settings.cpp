#include "settings.h"

#include <Preferences.h>
#include <esp_random.h>

namespace cfg {

Settings S;
static Preferences prefs;

void load() {
  prefs.begin("w6", true);
  S.ssid = prefs.getString("ssid", "");
  S.pass = prefs.getString("pass", "");
  S.has_offset = prefs.isKey("utc_off");
  S.utc_offset = prefs.getInt("utc_off", 0);
  S.token = prefs.getString("token", "");
  S.face = prefs.getString("face", "classic");
  S.charging = prefs.getString("chg", "classic");
  S.clock_24h = prefs.getBool("h24", false);
  prefs.end();
}

void save() {
  prefs.begin("w6", false);
  prefs.putString("ssid", S.ssid);
  prefs.putString("pass", S.pass);
  if (S.has_offset) prefs.putInt("utc_off", S.utc_offset);
  prefs.putString("token", S.token);
  prefs.putString("face", S.face);
  prefs.putString("chg", S.charging);
  prefs.putBool("h24", S.clock_24h);
  for (const char *gone : {"place", "lat", "lon", "wxfx"})  // the weather location of older firmware: not kept
    if (prefs.isKey(gone)) prefs.remove(gone);
  prefs.end();
}

void erase() {
  prefs.begin("w6", false);
  prefs.clear();
  prefs.end();
  S = Settings();
}

String random_string(int n) {
  // esp_random() is a true random number while the radio is on
  static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";
  String s;
  for (int i = 0; i < n; i++) s += A[esp_random() % (sizeof A - 1)];
  return s;
}

void new_token() {
  S.token = random_string(24);  // about 118 bits
  save();
}

int boot_count() {
  prefs.begin("w6", false);
  int n = prefs.getInt("boots", 0) + 1;
  prefs.putInt("boots", n);
  prefs.end();
  return n;
}

void clear_boot_count() {
  prefs.begin("w6", false);
  prefs.putInt("boots", 0);
  prefs.end();
}

bool fresh_update() {
  prefs.begin("w6", true);
  bool f = prefs.getBool("fresh", false);
  prefs.end();
  return f;
}

int rotation() {
  Preferences hw;
  hw.begin("w6hw", true);
  int r = hw.getInt("rot", 1);
  hw.end();
  return r & 3;
}

void set_rotation(int quarter_turns) {
  Preferences hw;
  hw.begin("w6hw", false);
  hw.putInt("rot", quarter_turns & 3);
  hw.end();
}

void set_fresh_update(bool on) {
  prefs.begin("w6", false);
  prefs.putBool("fresh", on);
  prefs.end();
}

}  // namespace cfg
