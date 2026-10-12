// W6 screens, drawn with LVGL. Nothing here touches the hardware, Wi-Fi or the system clock, so the same code runs
// on the stand and in the desktop preview (src/preview). main.cpp feeds it the time and the requests that
// arrive over the HTTP API.
//
// What is on screen: the clock is the home screen. Now playing and the charging screen replace it while the phone
// reports them. A progress ring (from POST /api/progress) covers it while a job
// runs, and a notification (POST /api/notify) covers both for a few seconds. setup_screen() and message() replace
// the home screen until home() is called.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ui {

enum class State { Running, Success, Failed };
enum class Level { Info, Success, Warning, Error };

struct Clock {
  bool valid = false;  // false until the time has come from the network; the clock then shows --:--
  int hour = 0, minute = 0, second = 0, ms = 0;  // local time
  int weekday = 0, day = 1, month = 1;           // weekday 0 = Sunday, month 1..12
};

void init();
// Call every frame. Animates the rings and moves back to the home screen when a progress or notification expires.
void tick(uint32_t now_ms, const Clock &c);

void set_wifi(bool connected);  // false shows a small red Wi-Fi mark on the clock
// 12-hour clock with AM/PM (the default, false) or 24-hour, for every time on screen
void set_24h(bool on);
// A time of day in the chosen format: "4:20 PM" or "16:20". For text built outside ui.cpp.
void hhmm(char *buf, size_t n, int minute_of_day);
// The 4:20 lava lamp: soft blobs rising behind "4:20". It plays by itself once a day from 16:20 to 16:21 (from when
// the clock is showing, if something else was up at 16:20:00); fun() plays it now for 15 seconds (POST /api/fun). It
// only ever replaces the clock.
void fun(uint32_t now_ms);
// Plays a show on demand (POST /api/fun): only "420" now. False for a name it doesn't know.
bool show(const char *name, uint32_t now_ms);

struct Progress {
  const char *title = "";
  int percent = -1;  // 0..100, or -1 when the total is not known (the ring then spins)
  State state = State::Running;
  const char *detail = "";  // a short second line, e.g. "12 / 40 resources"
  const char *icon = nullptr;  // an ICON_ string; nullptr uses ICON_TERMINAL
  uint32_t ttl_s = 600;  // the ring goes away when this long passes with no update
};
// A new title restarts the elapsed time shown under the ring.
void progress(const Progress &p, uint32_t now_ms);
void clear_progress();
void notify(const char *text, Level level, uint32_t seconds, uint32_t now_ms);
void clear_notify();

// The phone's battery, sent by an automation app on the phone (POST /api/phone). While a phone reports that it is
// charging, a charging screen replaces the clock: the charge as a ring and a big percentage, the charging power when
// the phone reports it, and a small clock. A phone that isn't charging, or stops updating for ttl_s, leaves the clock.
struct Phone {
  int percent = 0;        // 0..100
  bool charging = false;
  float watts = -1;       // < 0: not reported
};
void phone(const Phone &p, uint32_t ttl_s, uint32_t now_ms);
void clear_phone();
// How the charging screen looks. Classic: the ring, the percentage and a breathing bolt. Warp: flying through a star
// tunnel towards a portal in the charge colour, faster the more power the phone takes. Swarm: the percentage is made
// of particles that buzz round their places and stream in from the rim, more with more power. Warp and Swarm compute
// every pixel, like the living faces, and need ~22 KB while they show.
enum class ChargingStyle { Classic, Warp, Swarm, Count };
void set_charging_style(ChargingStyle s);
ChargingStyle charging_style_named(const char *name);  // anything unknown: classic
const char *charging_style_name(ChargingStyle s);      // "classic", "warp", "swarm"
bool charging_style_exists(const char *name);

// Clock faces. Classic: digital, the ring fills once a minute. Analog: hands and ticks.
//   Stack: big hours over minutes, the date at the sides. StackCalm: cream and brown, the seconds as dots.
//   Orbit: 60 dots round the edge light up through the hour. OrbitNight: the same in dim red.
//   Segment, SegmentTeal: seven-segment digits with the unlit segments faintly showing, and the seconds.
//   Words: the time in words to five minutes ("nearly ten past ten"), and a dot where the hour hand would be.
//   Walnut: analog with numerals, a date window and a seconds hand. Minimal: two hands and a minutes track.
//   Terminal, TerminalAmber: a shell prompt with dot-matrix digits and the last progress job.
// Living faces, every pixel computed: Wormhole (a tunnel flying past behind the time), Aurora (plasma in colours that
// follow the time of day), Galaxy (a turning spiral galaxy), Silk (folded bands of light), Sky (the sky for the time of
// day, sun or moon, stars) and Grain (an analog dial in walnut wood).
enum class Face {
  Classic, Analog, Stack, StackCalm, Orbit, OrbitNight, Segment, SegmentTeal, Words, Walnut, Minimal, Terminal,
  TerminalAmber, Wormhole, Aurora, Galaxy, Silk, Sky, Grain, Count
};
void set_face(Face f);
Face face_named(const char *name);  // the API's name for a face; anything unknown: classic
const char *face_name(Face f);      // "classic", "stack_calm", ...
bool face_exists(const char *name);
uint32_t living_frames();  // frames a living face or animated charging screen has drawn since the start: two readings a second apart give its rate

// A countdown (POST /api/timer), e.g. a 25-minute focus timer: the ring empties as time runs out, then "Time's up"
// for a minute. It covers the clock and the charging screen, not a notification.
void timer(const char *label, uint32_t seconds, uint32_t now_ms);
void clear_timer();
int32_t timer_left_s(uint32_t now_ms);  // -1 when no timer is running

// Now playing, sent by the W6 app (POST /api/media): round album art, the title and artist, and the track's position
// on the ring. Shown while music plays, and for a few seconds after it pauses, in place of the clock and the charging
// screen. It goes away when ttl_s passes with no update.
struct Media {
  const char *title = "", *artist = "";
  bool playing = false;
  int32_t position_ms = -1, duration_ms = -1;  // -1: not known, and the ring stays empty
};
void media(const Media &m, uint32_t ttl_s, uint32_t now_ms);
void clear_media();
// Album art is ART_SIZE x ART_SIZE pixels of RGB565, high byte first (ART_BYTES). Fill art_buffer(), then call
// art_ready(true) to show it, or art_ready(false) to show a music note instead.
constexpr int ART_SIZE = 112, ART_BYTES = ART_SIZE * ART_SIZE * 2;
// The buffer is allocated on first use and freed when now playing goes away; art_buffer() returns nullptr when there
// isn't enough memory.
uint8_t *art_buffer();
void art_ready(bool ok);
bool art_loaded();  // art is on screen; false after it was freed, so it has to be sent again


// Pairing the W6 app (POST /api/pair): a QR code the app scans, and the same code as text to type instead. It
// covers everything for `seconds`, with the ring counting down, or until clear_pair().
void pair_screen(const char *qr_payload, const char *code, uint32_t seconds, uint32_t now_ms);
void clear_pair();

void home();                                                   // back to the clock
void setup_screen(const char *ap_ssid, const char *ap_pass);   // QR code that joins the setup network
void message(const char *icon, const char *line1, const char *line2);  // icon: one of the ICON_ strings below

// Icons in the generated Font Awesome font (src/fonts), as UTF-8
#define ICON_WIFI "\xEF\x87\xAB"       // f1eb
#define ICON_CHECK "\xEF\x80\x8C"      // f00c
#define ICON_TIMES "\xEF\x80\x8D"      // f00d
#define ICON_WARNING "\xEF\x81\xB1"    // f071
#define ICON_INFO "\xEF\x81\x9A"       // f05a
#define ICON_SYNC "\xEF\x80\xA1"       // f021
#define ICON_TERMINAL "\xEF\x84\xA0"   // f120
#define ICON_DOWNLOAD "\xEF\x80\x99"   // f019
#define ICON_GITHUB "\xEF\x82\x9B"     // f09b
#define ICON_CUBES "\xEF\x86\xB2"      // f1b2
#define ICON_BOLT "\xEF\x83\xA7"       // f0e7
#define ICON_MUSIC "\xEF\x80\x81"      // f001
#define ICON_PAUSE "\xEF\x81\x8C"      // f04c

}  // namespace ui
