// Settings kept in the ESP32's flash (NVS), never in the source: Wi-Fi, time zone, API token.
#pragma once
#include <Arduino.h>

namespace cfg {

struct Settings {
  String ssid, pass;     // home Wi-Fi
  int32_t utc_offset = 0;  // the clock's time zone: seconds east of UTC, from the setup page or the app
  bool has_offset = false;
  String token;          // the API token; empty until the first setup makes one
  String face;           // a ui::face_name(): "classic" (default), "analog", "stack", ...
  String charging;       // a ui::charging_style_name(): "classic" (default), "warp" or "swarm"
  bool clock_24h = false;  // false: 12-hour with AM/PM
};
extern Settings S;

void load();
void save();
void erase();  // forgets everything: Wi-Fi, time zone, token
String random_string(int n);  // letters and digits without look-alikes; call only once Wi-Fi is running
void new_token();

// Counts boots that happen within a few seconds of the last one, so unplugging the stand three times in a row can
// open setup. Call boot_count() early in setup(), and clear_boot_count() once the stand has been up for a while.
int boot_count();
void clear_boot_count();

// Set after an over-the-air update and cleared once the new firmware has run for a minute. If the new firmware
// keeps crashing before that, main.cpp boots the previous one again.
bool fresh_update();
void set_fresh_update(bool on);

// Screen rotation in quarter turns clockwise (0-3) from the panel's own orientation. It depends on how the display is
// mounted, not on whose stand it is, so it lives in its own namespace and erase() leaves it alone. In the W6 base the
// header tab points to the right, which needs one quarter turn; that is the default.
int rotation();
void set_rotation(int quarter_turns);

}  // namespace cfg
