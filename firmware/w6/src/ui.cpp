// W6 screens. See ui.h for what each one is for. The panel is a 240 px circle, so every layout below is centred and
// kept inside the circle at its own height.
#include "ui.h"

#include <lvgl.h>

#include "charging_fx.h"
#include "living_fx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(w6_clock_68);
LV_FONT_DECLARE(w6_num_50);
LV_FONT_DECLARE(w6_text_24);
LV_FONT_DECLARE(w6_text_18);
LV_FONT_DECLARE(w6_text_14);
LV_FONT_DECLARE(w6_icons_34);
LV_FONT_DECLARE(w6_icons_18);
LV_FONT_DECLARE(w6_words_36);  // lowercase only, for the Words face
LV_FONT_DECLARE(w6_words_48);

namespace ui {

// ---- colours
static const uint32_t C_TEXT = 0xF2F2F2, C_DIM = 0x8C8C8C, C_TRACK = 0x1E1E1E;
static const uint32_t C_ACCENT = 0xFFA94D;  // warm amber, goes with the walnut-brown stand
static const uint32_t C_RUN = 0x4CC9F0, C_OK = 0x52D273, C_WARN = 0xFFC94D, C_FAIL = 0xFF5C5C;
static const uint32_t C_FLOW = 0xC9F7D6;  // the light streak on the charging ring

static lv_color_t col(uint32_t hex) { return lv_color_hex(hex); }

// ---- helpers
static lv_obj_t *new_screen() {
  lv_obj_t *s = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t colour, int y, int width = 0) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, "");
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col(colour), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  if (width) {
    lv_obj_set_size(l, width, lv_font_get_line_height(font));  // one line, cut with dots
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  }
  lv_obj_align(l, LV_ALIGN_CENTER, 0, y);
  return l;
}

// A ring around the edge: a grey track and a coloured indicator whose ends are set with set_arc().
static lv_obj_t *ring(lv_obj_t *parent, int diameter, int width, uint32_t track) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, diameter, diameter);
  lv_obj_center(a);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(a, 0, 0);
  lv_arc_set_rotation(a, 270);  // 0 degrees at 12 o'clock
  lv_arc_set_bg_angles(a, 0, 360);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, col(track), LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(a, false, LV_PART_MAIN);
  lv_arc_set_angles(a, 0, 0);
  return a;
}

static void set_arc(lv_obj_t *a, int start, int end) {  // degrees clockwise from 12 o'clock; end may exceed 360
  start = ((start % 360) + 360) % 360;
  end = ((end % 360) + 360) % 360;
  if (lv_arc_get_angle_start(a) != start || lv_arc_get_angle_end(a) != end) lv_arc_set_angles(a, start, end);
}

static void set_text(lv_obj_t *l, const char *s) {  // avoids redrawing a label whose text has not changed
  if (strcmp(lv_label_get_text(l), s) != 0) lv_label_set_text(l, s);
}

static void set_colour(lv_obj_t *o, uint32_t hex, lv_part_t part = LV_PART_MAIN) {
  lv_obj_set_style_text_color(o, col(hex), part);
  lv_obj_set_style_arc_color(o, col(hex), part);
}

// Setting a style redraws the object even when the value is the same, and the screen code runs every loop (a few
// milliseconds), so values that are set on every pass go through these. Unguarded, one bobbing icon kept the chip a
// third busy redrawing it.
static void set_opa(lv_obj_t *o, lv_opa_t v) {
  if (lv_obj_get_style_opa(o, LV_PART_MAIN) != v) lv_obj_set_style_opa(o, v, 0);
}

static void set_arc_opa(lv_obj_t *o, lv_opa_t v) {
  if (lv_obj_get_style_arc_opa(o, LV_PART_INDICATOR) != v) lv_obj_set_style_arc_opa(o, v, LV_PART_INDICATOR);
}

static void show_obj(lv_obj_t *o, bool on) {  // showing or hiding redraws the whole object, changed or not
  if (on != lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// Copies at most n bytes of s into d without cutting a UTF-8 character in half.
static void copy_utf8(char *d, const char *s, size_t n) {
  size_t len = strlen(s ? s : "");
  if (len >= n) {
    len = n - 1;
    while (len && ((unsigned char)s[len] & 0xC0) == 0x80) len--;
  }
  memcpy(d, s ? s : "", len);
  d[len] = 0;
}

// ---- state
enum class Base { Clock, Setup, Message };
static Base base = Base::Clock;
static lv_obj_t *current = nullptr;

static struct {
  lv_obj_t *scr, *ring, *time, *ampm, *date, *nowifi;
  int shown_min = -1, shown_day = -1;
  bool shown_valid = false;
} ck;

static Face face = Face::Classic;

static struct {  // the analog face
  lv_obj_t *scr, *ticks[12], *hour, *minute, *second, *cap, *date, *nowifi;
  lv_point_t tick_pts[12][2], hour_pts[2], min_pts[2], sec_pts[2];
  int shown_sec = -1, shown_day = -1;
} an;

// The faces after Analog. Only the chosen one exists: it is built when it is picked and deleted when another one is,
// so a dozen faces cost the memory of one. Dots, segments and dot-matrix digits are painted by the draw callback of a
// single object (art) rather than being an object each, which took 10-35 KB of LVGL's pool per face.
struct Extra {
  Face built = Face::Classic;  // Classic: none built
  lv_obj_t *scr = nullptr, *art = nullptr;
  lv_obj_t *a = nullptr, *b = nullptr, *date = nullptr, *day = nullptr, *ampm = nullptr, *month = nullptr;
  lv_obj_t *info = nullptr, *time = nullptr, *cmd = nullptr, *job = nullptr;
  lv_obj_t *result = nullptr, *cursor = nullptr, *nowifi = nullptr, *ring = nullptr, *hand[3] = {};
  lv_obj_t *time_sh = nullptr, *date_sh = nullptr, *shadow[3] = {};  // living faces: drop shadows
  lv_point_t pts[3][2], spts[3][2];
  uint32_t t0 = 0, frame_at = 0;  // living faces: when the face appeared, and its last frame
  uint32_t tried_at = 0;          // living faces: when the tables last couldn't be allocated
  bool dirty = true;  // something the face shows changed: redo it on the next tick
  bool drawn = false;
  uint32_t job_s = UINT32_MAX;  // Terminal: the second its job line was written
  Clock c;            // the time it shows
};
static Extra xf;
static bool online = true;
static lv_obj_t *fading = nullptr;  // the screen fading in, if any
static void use_extra(Face f);

static struct {  // the countdown timer
  lv_obj_t *scr, *ring, *label, *time, *clock;
  bool active = false;
  char label_txt[32] = "";
  uint32_t end = 0, total = 0, done_at = 0;
  int shown_min = -1;
} tm;

static struct {
  lv_obj_t *scr, *ring, *flow, *icon, *pct, *detail, *time;  // ring, flow and icon: the classic style only
  lv_obj_t *art, *pct_sh, *detail_sh, *time_sh;  // Warp and Swarm: the painted background, and the text's shadows
  ChargingStyle style = ChargingStyle::Classic;
  bool active = false;
  Phone p;
  uint32_t updated = 0, ttl_ms = 0;
  int shown_min = -1;
  float shown = 0;  // the ring and the number ease towards p.percent; reset to 0 when the screen appears
  uint32_t t0 = 0, frame_at = 0;  // Warp and Swarm: when the screen appeared, and its last frame
  int32_t full_at = -1;           // when the shown charge reached 100 %, -1 before
  bool fx_tried = false;          // the tables were asked for since the screen appeared (they may not fit)
} ph;

static struct {
  lv_obj_t *scr, *ring, *icon, *title, *pct, *detail, *state;
  bool active = false;
  State st = State::Running;
  int percent = -1;
  float shown = 0;  // the ring eases towards percent
  uint32_t started = 0, updated = 0, ttl_ms = 0, finished = 0;
  char title_txt[64] = "";
} pg;

static struct {
  lv_obj_t *scr, *ring, *icon, *text;
  bool active = false;
  uint32_t until = 0;
} nt;

static struct {  // now playing
  lv_obj_t *scr, *ring, *art_box, *art, *note, *pause, *title, *artist, *foot;
  bool active = false, has_art = false;
  Media m;
  char title_txt[96] = "", artist_txt[64] = "";
  uint32_t updated = 0, ttl_ms = 0, paused_at = 0;
  int shown_min = -1, shown_pct = -2;
} md;
// The art takes 25 KB, a fifth of the free memory, so it exists only while now playing has art to show.
static uint8_t *art_px = nullptr;
static lv_img_dsc_t art_dsc;
constexpr uint32_t PAUSED_SHOW_MS = 8000;  // a paused track stays up this long, so a quick pause doesn't flicker

static struct { lv_obj_t *scr, *qr, *ssid, *pass; } su;
static struct {  // pairing the app
  lv_obj_t *scr, *ring, *qr, *code;
  bool active = false;
  uint32_t until = 0, total = 0;
} pr;
static struct { lv_obj_t *scr, *icon, *line1, *line2; } ms;

// The 4:20 lava lamp: soft blobs rise and wobble behind a calm "4:20" on a dusk gradient, for FUN_MS, once a day at
// 16:20 (or on POST /api/fun). It only ever replaces the clock.
struct Blob { int16_t x, size, speed, wob; float phase; uint32_t colour; };
static const Blob BLOBS[] = {
  {-60, 78, 34, 20, 0.0f, 0xFF6B9A}, {45, 96, 26, 26, 1.7f, 0xFF9F43}, {-10, 64, 40, 18, 3.1f, 0xB57BFF},
  {70, 56, 31, 14, 4.2f, 0xFF5F6D}, {-75, 50, 37, 16, 5.5f, 0xFFC15E}, {15, 86, 23, 22, 2.4f, 0xD46BFF},
};
constexpr int NBLOBS = sizeof BLOBS / sizeof BLOBS[0];
constexpr uint32_t FUN_MS = 15000, FUN_FRAME_MS = 50;  // on demand; 20 frames a second: the blobs cover most of the screen
static struct {
  lv_obj_t *scr, *blob[NBLOBS], *time;
  uint32_t started = 0, frame = 0;
  bool active = false;
  bool daily = false;    // started by the clock: runs to the end of 16:20, not for FUN_MS
  int played_day = -1;  // the day of the month it last played at 16:20
} lv;

// 12-hour clock with AM/PM (the default) or 24-hour (POST /api/settings {"clock_24h": true})
static bool h24 = false;

void hhmm(char *buf, size_t n, int minute_of_day) {
  int h = minute_of_day / 60 % 24, m = minute_of_day % 60;
  if (h24) snprintf(buf, n, "%02d:%02d", h, m);
  else snprintf(buf, n, "%d:%02d %s", h % 12 ? h % 12 : 12, m, h < 12 ? "AM" : "PM");
}

// ---- building the screens
constexpr int CK_TIME_Y = -10;  // the digits, a little above the middle, and the date under them

static void build_clock() {
  ck.scr = new_screen();
  ck.ring = ring(ck.scr, 236, 4, C_TRACK);
  lv_obj_set_style_arc_color(ck.ring, col(C_ACCENT), LV_PART_INDICATOR);
  ck.time = label(ck.scr, &w6_clock_68, C_TEXT, CK_TIME_Y);
  ck.ampm = label(ck.scr, &w6_text_18, C_DIM, 0);  // placed beside the digits by update_clock()
  lv_label_set_text(ck.time, "--:--");
  ck.date = label(ck.scr, &w6_text_18, C_DIM, CK_TIME_Y + 44);
  ck.nowifi = label(ck.scr, &w6_icons_18, C_FAIL, 92);
  lv_label_set_text(ck.nowifi, ICON_WIFI);
  show_obj(ck.nowifi, false);
}

static void build_progress() {
  pg.scr = new_screen();
  pg.ring = ring(pg.scr, 232, 12, C_TRACK);
  pg.icon = label(pg.scr, &w6_icons_18, C_RUN, -86);
  pg.title = label(pg.scr, &w6_text_18, C_TEXT, -58, 156);
  lv_label_set_long_mode(pg.title, LV_LABEL_LONG_SCROLL_CIRCULAR);  // a long title scrolls rather than being cut
  pg.pct = label(pg.scr, &w6_num_50, C_TEXT, -6);
  pg.detail = label(pg.scr, &w6_text_14, C_DIM, 34, 184);
  pg.state = label(pg.scr, &w6_text_18, C_RUN, 60, 152);
}

static void build_notify() {
  nt.scr = new_screen();
  nt.ring = ring(nt.scr, 236, 4, C_TRACK);
  lv_arc_set_angles(nt.ring, 0, 360);
  nt.icon = label(nt.scr, &w6_icons_34, C_RUN, -66);
  nt.text = lv_label_create(nt.scr);
  lv_obj_set_width(nt.text, 176);
  lv_obj_set_height(nt.text, LV_SIZE_CONTENT);
  lv_obj_set_style_text_color(nt.text, col(C_TEXT), 0);
  lv_obj_set_style_text_align(nt.text, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(nt.text, LV_LABEL_LONG_WRAP);
  lv_obj_align(nt.text, LV_ALIGN_CENTER, 0, 14);
}

static lv_obj_t *hand(lv_obj_t *parent, lv_point_t *pts, int width, uint32_t colour) {
  lv_obj_t *l = lv_line_create(parent);
  pts[0].x = pts[1].x = 120, pts[0].y = pts[1].y = 120;
  lv_line_set_points(l, pts, 2);
  lv_obj_set_pos(l, 0, 0);
  lv_obj_set_style_line_width(l, width, 0);
  lv_obj_set_style_line_color(l, col(colour), 0);
  lv_obj_set_style_line_rounded(l, true, 0);
  return l;
}

// Points a hand of the given length (from the centre, and tail behind it) at angle degrees clockwise from 12
static void point_hand(lv_obj_t *l, lv_point_t *pts, float deg, int length, int tail) {
  float a = deg * 3.14159265f / 180.0f, s = sinf(a), c = cosf(a);
  pts[0].x = 120 - (lv_coord_t)lroundf(tail * s), pts[0].y = 120 + (lv_coord_t)lroundf(tail * c);
  pts[1].x = 120 + (lv_coord_t)lroundf(length * s), pts[1].y = 120 - (lv_coord_t)lroundf(length * c);
  lv_line_set_points(l, pts, 2);
}

static void build_analog() {
  an.scr = new_screen();
  for (int i = 0; i < 12; i++) {  // hour ticks; 12, 3, 6 and 9 longer and brighter
    bool major = i % 3 == 0;
    float a = i * 30 * 3.14159265f / 180.0f, s = sinf(a), c = cosf(a);
    int r0 = major ? 98 : 104, r1 = 114;
    an.tick_pts[i][0] = {(lv_coord_t)lroundf(120 + r0 * s), (lv_coord_t)lroundf(120 - r0 * c)};
    an.tick_pts[i][1] = {(lv_coord_t)lroundf(120 + r1 * s), (lv_coord_t)lroundf(120 - r1 * c)};
    an.ticks[i] = lv_line_create(an.scr);
    lv_line_set_points(an.ticks[i], an.tick_pts[i], 2);
    lv_obj_set_pos(an.ticks[i], 0, 0);
    lv_obj_set_style_line_width(an.ticks[i], major ? 4 : 2, 0);
    lv_obj_set_style_line_color(an.ticks[i], col(major ? C_TEXT : C_DIM), 0);
    lv_obj_set_style_line_rounded(an.ticks[i], true, 0);
  }
  an.date = label(an.scr, &w6_text_14, C_DIM, 46);
  an.nowifi = label(an.scr, &w6_icons_18, C_FAIL, 84);
  lv_label_set_text(an.nowifi, ICON_WIFI);
  show_obj(an.nowifi, false);
  an.hour = hand(an.scr, an.hour_pts, 7, C_TEXT);
  an.minute = hand(an.scr, an.min_pts, 4, C_TEXT);
  an.second = hand(an.scr, an.sec_pts, 2, C_ACCENT);
  an.cap = lv_obj_create(an.scr);
  lv_obj_remove_style_all(an.cap);
  lv_obj_set_size(an.cap, 10, 10);
  lv_obj_set_style_radius(an.cap, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(an.cap, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(an.cap, col(C_ACCENT), 0);
  lv_obj_center(an.cap);
}

static void build_timer() {
  tm.scr = new_screen();
  tm.ring = ring(tm.scr, 232, 12, C_TRACK);
  lv_obj_set_style_arc_color(tm.ring, col(C_RUN), LV_PART_INDICATOR);
  tm.label = label(tm.scr, &w6_text_18, C_RUN, -58, 150);
  tm.time = label(tm.scr, &w6_num_50, C_TEXT, -4);
  tm.clock = label(tm.scr, &w6_text_18, C_DIM, 60);
}

static void paint_charging(lv_event_t *e) { chfx::paint(lv_event_get_draw_ctx(e)); }

// A label with a soft drop shadow (returned through sh), for text on a moving picture
static lv_obj_t *shadowed(const lv_font_t *font, uint32_t colour, int y, lv_obj_t **sh = nullptr) {
  lv_obj_t *s = label(ph.scr, font, 0x000000, y + 2);
  lv_obj_align(s, LV_ALIGN_CENTER, 2, y + 2);
  lv_obj_set_style_text_opa(s, 170, 0);
  if (sh) *sh = s;
  return label(ph.scr, font, colour, y);
}

static void build_charging() {
  ph.scr = new_screen();
  ph.ring = ph.flow = ph.icon = ph.art = ph.pct_sh = ph.detail_sh = ph.time_sh = nullptr;
  if (ph.style != ChargingStyle::Classic) {  // Warp and Swarm: the picture is painted, the text drawn on top
    ph.art = lv_obj_create(ph.scr);
    lv_obj_remove_style_all(ph.art);
    lv_obj_set_size(ph.art, 240, 240);
    lv_obj_clear_flag(ph.art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ph.art, paint_charging, LV_EVENT_DRAW_MAIN, nullptr);
    bool warp = ph.style == ChargingStyle::Warp;
    // Swarm draws the number with its particles; the label is there for when there was no memory for them
    ph.pct = shadowed(&w6_num_50, C_TEXT, warp ? -8 : 0, &ph.pct_sh);
    ph.detail = shadowed(warp ? &w6_text_18 : &w6_text_24, C_OK, warp ? 26 : 42, &ph.detail_sh);
    ph.time = shadowed(&w6_text_18, 0xBDBDBD, warp ? 88 : 74, &ph.time_sh);
    return;
  }
  ph.ring = ring(ph.scr, 232, 12, C_TRACK);
  lv_obj_set_style_arc_color(ph.ring, col(C_OK), LV_PART_INDICATOR);
  ph.flow = ring(ph.scr, 232, 12, C_TRACK);  // a bright streak that runs along the filled part while charging
  lv_obj_set_style_arc_opa(ph.flow, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_color(ph.flow, col(C_FLOW), LV_PART_INDICATOR);
  ph.icon = label(ph.scr, &w6_icons_34, C_OK, -62);
  lv_label_set_text(ph.icon, ICON_BOLT);
  ph.pct = label(ph.scr, &w6_num_50, C_TEXT, -4);
  ph.detail = label(ph.scr, &w6_text_24, C_OK, 40);
  ph.time = label(ph.scr, &w6_text_18, C_DIM, 74);
}

static void build_media() {
  md.scr = new_screen();
  md.ring = ring(md.scr, 236, 4, C_TRACK);  // how far into the track
  lv_obj_set_style_arc_color(md.ring, col(C_ACCENT), LV_PART_INDICATOR);
  md.art_box = lv_obj_create(md.scr);  // a circle that clips the square art
  lv_obj_remove_style_all(md.art_box);
  lv_obj_set_size(md.art_box, ART_SIZE, ART_SIZE);
  lv_obj_set_style_radius(md.art_box, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(md.art_box, true, 0);
  lv_obj_set_style_bg_color(md.art_box, col(C_TRACK), 0);
  lv_obj_set_style_bg_opa(md.art_box, LV_OPA_COVER, 0);
  lv_obj_clear_flag(md.art_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(md.art_box, LV_ALIGN_CENTER, 0, -44);
  art_dsc.header.always_zero = 0;
  art_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  art_dsc.header.w = ART_SIZE, art_dsc.header.h = ART_SIZE;
  art_dsc.data_size = ART_BYTES;
  art_dsc.data = nullptr;
  md.art = lv_img_create(md.art_box);
  lv_obj_center(md.art);
  show_obj(md.art, false);
  md.note = lv_label_create(md.art_box);
  lv_obj_set_style_text_font(md.note, &w6_icons_34, 0);
  lv_obj_set_style_text_color(md.note, col(C_ACCENT), 0);
  lv_label_set_text(md.note, ICON_MUSIC);
  lv_obj_center(md.note);
  md.pause = lv_label_create(md.art_box);
  lv_obj_set_style_text_font(md.pause, &w6_icons_34, 0);
  lv_obj_set_style_text_color(md.pause, col(C_TEXT), 0);
  lv_label_set_text(md.pause, ICON_PAUSE);
  lv_obj_center(md.pause);
  show_obj(md.pause, false);
  md.title = label(md.scr, &w6_text_18, C_TEXT, 36, 204);
  lv_label_set_long_mode(md.title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  md.artist = label(md.scr, &w6_text_14, C_DIM, 60, 168);
  md.foot = label(md.scr, &w6_text_14, C_DIM, 86);
}

static void build_setup() {
  su.scr = new_screen();
  lv_obj_t *t = label(su.scr, &w6_text_14, C_DIM, -92);
  lv_label_set_text(t, "Scan to set up W6");
  su.qr = lv_qrcode_create(su.scr, 116, lv_color_black(), lv_color_white());
  lv_obj_set_style_border_color(su.qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(su.qr, 4, 0);
  lv_obj_align(su.qr, LV_ALIGN_CENTER, 0, -14);
  su.ssid = label(su.scr, &w6_text_18, C_TEXT, 64);
  su.pass = label(su.scr, &w6_text_14, C_DIM, 86);
}

static void build_pair() {
  pr.scr = new_screen();
  pr.ring = ring(pr.scr, 236, 4, C_TRACK);  // time left
  lv_obj_set_style_arc_color(pr.ring, col(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_t *t = label(pr.scr, &w6_text_14, C_DIM, -86);
  lv_label_set_text(t, "Scan in the W6 app");
  pr.qr = lv_qrcode_create(pr.scr, 112, lv_color_black(), lv_color_white());
  lv_obj_set_style_border_color(pr.qr, lv_color_white(), 0);
  lv_obj_set_style_border_width(pr.qr, 4, 0);
  lv_obj_align(pr.qr, LV_ALIGN_CENTER, 0, -14);
  pr.code = label(pr.scr, &w6_text_24, C_TEXT, 62);
  lv_obj_t *h = label(pr.scr, &w6_text_14, C_DIM, 88);
  lv_label_set_text(h, "or type this code");
}

bool show(const char *name, uint32_t now) {
  if (!strcmp(name, "420")) fun(now);
  else return false;
  return true;
}

static void build_lava() {  // fun() builds it; tick() deletes it once it has gone
  lv.scr = new_screen();
  lv_obj_set_style_bg_color(lv.scr, col(0x2A0E3F), 0);
  lv_obj_set_style_bg_grad_color(lv.scr, col(0x0B0614), 0);
  lv_obj_set_style_bg_grad_dir(lv.scr, LV_GRAD_DIR_VER, 0);
  for (int i = 0; i < NBLOBS; i++) {
    lv_obj_t *b = lv.blob[i] = lv_obj_create(lv.scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, col(BLOBS[i].colour), 0);
    lv_obj_set_style_bg_opa(b, 150, 0);
  }
  lv.time = label(lv.scr, &w6_clock_68, 0xFFF4E8, -8);
  lv_label_set_text(lv.time, "4:20");
  lv_obj_t *t = label(lv.scr, &w6_text_18, 0xF2C7E8, 46);
  lv_label_set_text(t, "chill time");
}

static void update_lava(uint32_t now) {
  uint32_t frame = (now - lv.started) / FUN_FRAME_MS;
  if (frame == lv.frame) return;  // nothing moves between frames, so nothing is redrawn
  lv.frame = frame;
  float t = frame * FUN_FRAME_MS / 1000.0f;
  for (int i = 0; i < NBLOBS; i++) {
    const Blob &b = BLOBS[i];
    // rise from below the screen to above it and start again; the size breathes a little
    float travel = 240 + b.size;
    float y = 120 + b.size / 2 - fmodf(b.phase * 40 + t * b.speed, travel);
    float x = b.x + b.wob * sinf(t * 0.9f + b.phase);
    int d = (int)(b.size * (1 + 0.08f * sinf(t * 1.7f + b.phase)));
    lv_obj_set_size(lv.blob[i], d, (int)(d * (1 + 0.12f * sinf(t * 1.3f + b.phase * 2))));
    lv_obj_align(lv.blob[i], LV_ALIGN_CENTER, (lv_coord_t)x, (lv_coord_t)y);
  }
  lv_opa_t opa = (lv_opa_t)(255 * (0.9f + 0.1f * sinf(t * 2.0f)));  // 4:20 breathes slowly
  if (lv_obj_get_style_text_opa(lv.time, LV_PART_MAIN) != opa) lv_obj_set_style_text_opa(lv.time, opa, 0);
}

void fun(uint32_t now) {
  if (!lv.scr) build_lava();
  lv.active = true, lv.daily = false, lv.started = now, lv.frame = UINT32_MAX;
}

static void build_message() {
  ms.scr = new_screen();
  ms.icon = label(ms.scr, &w6_icons_34, C_ACCENT, -44);
  ms.line1 = label(ms.scr, &w6_text_18, C_TEXT, 4);
  lv_obj_set_width(ms.line1, 180);
  lv_label_set_long_mode(ms.line1, LV_LABEL_LONG_WRAP);
  ms.line2 = label(ms.scr, &w6_text_14, C_DIM, 44);
  lv_obj_set_width(ms.line2, 170);
  lv_label_set_long_mode(ms.line2, LV_LABEL_LONG_WRAP);
}

void init() {
  lv_disp_t *d = lv_disp_get_default();  // what shows through a screen that is fading in: black, not LVGL's white
  lv_disp_set_bg_color(d, lv_color_black());
  lv_disp_set_bg_opa(d, LV_OPA_COVER);
  build_clock();
  build_progress();
  build_notify();
  build_charging();
  build_analog();
  build_timer();
  build_media();
  build_setup();
  build_pair();
  build_message();
  current = ck.scr;
  lv_scr_load(current);
}

// ---- updating
static void update_clock(const Clock &c) {
  if (!c.valid) {
    if (ck.shown_valid || ck.shown_min == -1) {
      lv_label_set_text(ck.time, "--:--");
      set_text(ck.ampm, "");
      lv_label_set_text(ck.date, "");
      set_arc(ck.ring, 0, 0);
      ck.shown_valid = false, ck.shown_min = -2, ck.shown_day = -1;
    }
    return;
  }
  if (!ck.shown_valid || c.minute != ck.shown_min) {
    if (h24) lv_label_set_text_fmt(ck.time, "%02d:%02d", c.hour, c.minute);
    else lv_label_set_text_fmt(ck.time, "%d:%02d", c.hour % 12 ? c.hour % 12 : 12, c.minute);
    set_text(ck.ampm, h24 ? "" : c.hour < 12 ? "AM" : "PM");
    // AM/PM sits at the top right of the digits, like a watch, and the two are centred together: "12:58 PM" would
    // otherwise touch the ring
    lv_obj_update_layout(ck.scr);
    int shift = h24 ? 0 : (lv_obj_get_width(ck.ampm) + 3) / 2;
    lv_obj_align(ck.time, LV_ALIGN_CENTER, -shift, CK_TIME_Y);
    if (!h24) lv_obj_align_to(ck.ampm, ck.time, LV_ALIGN_OUT_RIGHT_TOP, 3, 10);
    ck.shown_min = c.minute;
  }
  if (!ck.shown_valid || c.day != ck.shown_day) {
    static const char *const WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const MO[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    lv_label_set_text_fmt(ck.date, "%s  %d %s", WD[c.weekday % 7], c.day, MO[(c.month + 11) % 12]);
    ck.shown_day = c.day;
  }
  ck.shown_valid = true;
  // The ring fills once a minute, moving every 50 ms; 0 to 359 degrees so it never draws a full circle
  int deg = (c.second * 1000 + c.ms) * 360 / 60000;
  set_arc(ck.ring, 0, deg);
}

static void update_analog(const Clock &c) {
  if (!c.valid) {
    if (an.shown_sec != -2) {
      point_hand(an.hour, an.hour_pts, 0, 0, 0), point_hand(an.minute, an.min_pts, 0, 0, 0);
      point_hand(an.second, an.sec_pts, 0, 0, 0);
      set_text(an.date, "");
      an.shown_sec = -2, an.shown_day = -1;
    }
    return;
  }
  int sec = c.minute * 60 + c.second;
  if (sec != an.shown_sec) {  // the second hand ticks once a second
    an.shown_sec = sec;
    point_hand(an.hour, an.hour_pts, (c.hour % 12) * 30 + c.minute * 0.5f, 56, 10);
    point_hand(an.minute, an.min_pts, c.minute * 6 + c.second * 0.1f, 86, 12);
    point_hand(an.second, an.sec_pts, c.second * 6, 96, 18);
  }
  if (c.day != an.shown_day) {
    static const char *const WD[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    lv_label_set_text_fmt(an.date, "%s  %d", WD[c.weekday % 7], c.day);
    an.shown_day = c.day;
  }
}

static void update_timer(uint32_t now, const Clock &c) {
  int32_t left = (int32_t)(tm.end - now);
  char buf[16];
  if (left > 0) {
    int deg = (int)((int64_t)left * 360 / tm.total);
    set_arc(tm.ring, 0, deg >= 360 ? 359 : deg);
    uint32_t s = (left + 999) / 1000;
    if (s >= 3600) snprintf(buf, sizeof buf, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                            (unsigned long)(s % 60));
    else snprintf(buf, sizeof buf, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    set_text(tm.time, buf);
  } else {
    if (!tm.done_at) {
      tm.done_at = now;
      set_arc(tm.ring, 0, 359);
      lv_obj_set_style_arc_color(tm.ring, col(C_OK), LV_PART_INDICATOR);
      set_text(tm.label, "Time's up");
      lv_obj_set_style_text_color(tm.label, col(C_OK), 0);
      set_text(tm.time, "00:00");
    }
    uint32_t t = (now - tm.done_at) % 1000;  // the ring blinks for a minute, then the timer goes away
    set_arc_opa(tm.ring, t < 600 ? LV_OPA_COVER : LV_OPA_30);
    if (now - tm.done_at >= 60000) tm.active = false;
  }
  if (!c.valid) set_text(tm.clock, ""), tm.shown_min = -1;
  else if (c.minute != tm.shown_min) {
    char buf[12];
    hhmm(buf, sizeof buf, c.hour * 60 + c.minute);
    set_text(tm.clock, buf), tm.shown_min = c.minute;
  }
}

// The charging screen: only the phone's charge, big. Shown while a phone on the stand reports that it is charging.
// Animated in update_charging(): the ring sweeps up to the charge when the screen appears, a light streak runs along
// it every two seconds, and the bolt breathes. All of it is driven by the time passed in, so the preview matches.
static void show_phone() {
  if (!ph.active) return;
  char buf[16];
  if (ph.p.percent >= 100) snprintf(buf, sizeof buf, "Full");
  else if (ph.p.watts >= 0) snprintf(buf, sizeof buf, "%.1f W", ph.p.watts);
  else snprintf(buf, sizeof buf, "Charging");
  set_text(ph.detail, buf);
  if (ph.detail_sh) set_text(ph.detail_sh, buf);
  uint32_t c = ph.p.percent < 20 ? C_WARN : C_OK;
  set_colour(ph.detail, c);
  if (!ph.ring) return;  // Warp and Swarm have no ring, streak or bolt
  lv_obj_set_style_arc_color(ph.ring, col(c), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(ph.icon, col(c), 0);
  lv_obj_set_style_arc_color(ph.flow, col(ph.p.percent < 20 ? 0xFFE7A8 : C_FLOW), LV_PART_INDICATOR);
}

static constexpr uint32_t CHARGE_FRAME_MS = 66;

static void update_charging(uint32_t now) {
  float target = ph.p.percent;
  ph.shown += (target - ph.shown) * 0.08f;
  if (target - ph.shown < 0.3f && target - ph.shown > -0.3f) ph.shown = target;
  char buf[8];
  snprintf(buf, sizeof buf, "%d%%", (int)(ph.shown + 0.5f));
  set_text(ph.pct, buf);
  if (ph.art) {  // Warp and Swarm: a new picture 10 times a second, like the living faces
    if (ph.pct_sh) set_text(ph.pct_sh, buf);
    // Swarm spells the phone's number at once (its particles fly in); Warp follows the sweep up from empty
    bool swarm = ph.style == ChargingStyle::Swarm;
    float level = swarm ? (float)ph.p.percent : ph.shown;
    if (level < 100) ph.full_at = -1;
    else if (ph.full_at < 0) ph.full_at = (int32_t)(swarm && now == ph.t0 ? now - 5000 : now);  // already full: no burst
    bool fx = chfx::active();
    bool number = !fx || ph.style == ChargingStyle::Warp;  // Swarm's particles are the number
    show_obj(ph.pct, number), show_obj(ph.pct_sh, number);
    if (fx && now - ph.frame_at >= 100) {
      uint32_t dt = now - ph.frame_at > 200 ? 100 : now - ph.frame_at;
      ph.frame_at = now;
      chfx::frame(now - ph.t0, dt, level, ph.p.watts, ph.full_at < 0 ? -1 : (int32_t)(now - ph.full_at));
      lv_obj_invalidate(ph.art);
    }
    return;
  }
  int deg = (int)(ph.shown * 3.6f + 0.5f);
  set_arc(ph.ring, 0, deg >= 360 ? 359 : deg);

  // The streak and the bolt move in 66 ms steps (15 frames a second) rather than as often as LVGL allows (50):
  // the charging screen is on when the base is warmest. At 25 a second it kept the stand ~31 % busy.
  now -= now % CHARGE_FRAME_MS;

  // the streak: 28 degrees long, runs from 12 o'clock to the end of the fill, then pauses briefly
  bool flowing = ph.p.percent < 100 && ph.shown == target && deg > 40;
  if (flowing) {
    int head = (int)((now % 2000) * (deg + 120) / 2000);  // the extra 120 degrees is the pause at the end
    int tail = head - 28 < 0 ? 0 : head - 28;
    if (head > deg) head = deg;
    flowing = tail < head;
    if (flowing) set_arc(ph.flow, tail, head);
  }
  show_obj(ph.flow, flowing);  // once: each show or hide redraws the whole ring

  // the bolt breathes: 35 % to 100 % opacity and back every 1.6 s (a triangle wave, eased by the eye)
  uint32_t t = now % 1600;
  int opa = 90 + (int)(165 * (t < 800 ? t : 1600 - t) / 800);
  if (ph.p.percent >= 100) opa = 255;
  set_opa(ph.icon, (lv_opa_t)opa);
}

void phone(const Phone &p, uint32_t ttl_s, uint32_t now) {
  ph.p = p;
  ph.p.percent = p.percent < 0 ? 0 : p.percent > 100 ? 100 : p.percent;
  ph.active = true, ph.updated = now, ph.ttl_ms = ttl_s * 1000;
  show_phone();
}

void clear_phone() { ph.active = false; }

static const char *const CHARGING_NAMES[] = {"classic", "warp", "swarm"};
static_assert(sizeof CHARGING_NAMES / sizeof CHARGING_NAMES[0] == (size_t)ChargingStyle::Count, "a name for each");

const char *charging_style_name(ChargingStyle s) { return s < ChargingStyle::Count ? CHARGING_NAMES[(int)s] : "classic"; }

bool charging_style_exists(const char *n) {
  for (const char *k : CHARGING_NAMES)
    if (n && !strcmp(n, k)) return true;
  return false;
}

ChargingStyle charging_style_named(const char *n) {
  for (int i = 0; i < (int)ChargingStyle::Count; i++)
    if (n && !strcmp(n, CHARGING_NAMES[i])) return (ChargingStyle)i;
  return ChargingStyle::Classic;
}

// Rebuilds the charging screen in the new style; if it is showing, the new one takes its place at once.
void set_charging_style(ChargingStyle s) {
  if (s >= ChargingStyle::Count) s = ChargingStyle::Classic;
  if (s == ph.style && ph.scr) return;
  ph.style = s;
  lv_obj_t *old = ph.scr;
  chfx::end();
  build_charging();
  ph.shown_min = -1, ph.fx_tried = false, ph.frame_at = 0;
  show_phone();
  if (!old) return;
  if (current == old) current = ph.scr, lv_scr_load(ph.scr);
  if (fading == old) fading = nullptr;
  lv_obj_del(old);
}

static const char *const FACE_NAMES[] = {"classic", "analog", "stack", "stack_calm", "orbit", "orbit_night", "segment",
                                         "segment_teal", "words", "walnut", "minimal", "terminal", "terminal_amber",
                                         "wormhole", "aurora", "galaxy", "silk", "sky", "walnut_grain"};
static_assert(sizeof FACE_NAMES / sizeof FACE_NAMES[0] == (size_t)Face::Count, "a name for every face");

const char *face_name(Face f) { return f < Face::Count ? FACE_NAMES[(int)f] : "classic"; }

bool face_exists(const char *n) {
  for (const char *k : FACE_NAMES)
    if (n && !strcmp(n, k)) return true;
  return false;
}

Face face_named(const char *n) {
  for (int i = 0; i < (int)Face::Count; i++)
    if (n && !strcmp(n, FACE_NAMES[i])) return (Face)i;
  return Face::Classic;
}

void set_face(Face f) {
  if (f >= Face::Count) f = Face::Classic;
  face = f;
  use_extra(f);
  ck.shown_valid = false;
}

void timer(const char *label, uint32_t seconds, uint32_t now) {
  copy_utf8(tm.label_txt, label && *label ? label : "Timer", sizeof tm.label_txt);
  tm.total = seconds * 1000, tm.end = now + tm.total, tm.done_at = 0, tm.active = true;
  tm.shown_min = -1;
  set_text(tm.label, tm.label_txt);
  lv_obj_set_style_text_color(tm.label, col(C_RUN), 0);
  lv_obj_set_style_arc_color(tm.ring, col(C_RUN), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(tm.ring, LV_OPA_COVER, LV_PART_INDICATOR);
}

void clear_timer() { tm.active = false; }

int32_t timer_left_s(uint32_t now) {
  if (!tm.active) return -1;
  int32_t left = (int32_t)(tm.end - now);
  return left > 0 ? (left + 999) / 1000 : 0;
}

void set_24h(bool on) {
  if (on == h24) return;
  h24 = on;
  ck.shown_min = tm.shown_min = ph.shown_min = md.shown_min = -1;  // redraw every clock on its next tick
  xf.dirty = true;
}

void media(const Media &m, uint32_t ttl_s, uint32_t now) {
  bool was_playing = md.active && md.m.playing;
  copy_utf8(md.title_txt, m.title, sizeof md.title_txt);
  copy_utf8(md.artist_txt, m.artist, sizeof md.artist_txt);
  md.m = m;
  md.m.title = md.title_txt, md.m.artist = md.artist_txt;
  if (!m.playing && (was_playing || !md.active)) md.paused_at = now;
  md.active = true, md.updated = now, md.ttl_ms = ttl_s * 1000;
  set_text(md.title, md.title_txt);
  set_text(md.artist, md.artist_txt);
  show_obj(md.pause, !m.playing);
  lv_obj_set_style_img_opa(md.art, m.playing ? LV_OPA_COVER : LV_OPA_40, 0);
  lv_obj_set_style_text_opa(md.note, m.playing ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void free_art() {
  show_obj(md.art, false);
  show_obj(md.note, true);
  md.has_art = false;
  if (!art_px) return;
  lv_img_set_src(md.art, NULL);
  lv_img_cache_invalidate_src(&art_dsc);
  free(art_px);
  art_px = nullptr;
  art_dsc.data = nullptr;
}

void clear_media() {
  md.active = false;
  free_art();
}

uint8_t *art_buffer() {
  if (!art_px) {
    art_px = (uint8_t *)malloc(ART_BYTES);
    art_dsc.data = art_px;
  }
  return art_px;
}

bool art_loaded() { return md.has_art && art_px; }

void art_ready(bool ok) {
  if (!ok || !art_px) return free_art();
#if LV_COLOR_16_SWAP == 0  // the wire format is high byte first; the desktop preview keeps pixels in CPU order
  if (ok)
    for (int i = 0; i < ART_BYTES; i += 2) {
      uint8_t t = art_px[i];
      art_px[i] = art_px[i + 1], art_px[i + 1] = t;
    }
#endif
  md.has_art = true;
  lv_img_cache_invalidate_src(&art_dsc);
  lv_img_set_src(md.art, &art_dsc);
  lv_obj_invalidate(md.art);
  show_obj(md.art, true);
  show_obj(md.note, false);
}

static bool media_showing(uint32_t now) {
  if (!md.active) return false;
  return md.m.playing || now - md.paused_at < PAUSED_SHOW_MS;
}

static void update_media(uint32_t now, const Clock &c) {
  const Media &m = md.m;
  if (m.duration_ms > 0 && m.position_ms >= 0) {
    int64_t pos = m.position_ms + (m.playing ? (int64_t)(now - md.updated) : 0);
    if (pos > m.duration_ms) pos = m.duration_ms;
    int deg = (int)(pos * 360 / m.duration_ms);
    set_arc(md.ring, 0, deg >= 360 ? 359 : deg);
  } else {
    set_arc(md.ring, 0, 0);
  }
  int pct = ph.active && ph.p.charging ? ph.p.percent : -1;  // the charge, when the phone is charging under the music
  int minute = c.valid ? c.hour * 60 + c.minute : -1;
  if (minute != md.shown_min || pct != md.shown_pct) {
    md.shown_min = minute, md.shown_pct = pct;
    char buf[32] = "";
    char t[12];
    if (c.valid) hhmm(t, sizeof t, minute);
    if (c.valid && pct >= 0) snprintf(buf, sizeof buf, "%s  \xC2\xB7  %d%%", t, pct);
    else if (c.valid) snprintf(buf, sizeof buf, "%s", t);
    else if (pct >= 0) snprintf(buf, sizeof buf, "%d%%", pct);
    set_text(md.foot, buf);
  }
}

void set_wifi(bool connected) {
  online = connected;
  show_obj(ck.nowifi, !connected);
  show_obj(an.nowifi, !connected);
  if (xf.nowifi) show_obj(xf.nowifi, !connected);
}

static uint32_t state_colour(State s) { return s == State::Success ? C_OK : s == State::Failed ? C_FAIL : C_RUN; }

void progress(const Progress &p, uint32_t now) {
  if (!pg.active || strncmp(pg.title_txt, p.title ? p.title : "", sizeof pg.title_txt - 1) != 0) {
    pg.started = now;
    pg.shown = p.percent > 0 ? p.percent : 0;
  }
  copy_utf8(pg.title_txt, p.title, sizeof pg.title_txt);
  if (p.state != State::Running && (pg.st == State::Running || !pg.active)) pg.finished = now;
  pg.active = true;
  pg.st = p.state;
  pg.percent = p.percent < 0 ? -1 : p.percent > 100 ? 100 : p.percent;
  pg.updated = now;
  pg.ttl_ms = p.ttl_s * 1000;
  set_text(pg.title, pg.title_txt);
  set_text(pg.detail, p.detail ? p.detail : "");
  uint32_t c = state_colour(p.state);
  set_text(pg.icon, p.state == State::Success ? ICON_CHECK
                    : p.state == State::Failed ? ICON_TIMES
                    : (p.icon ? p.icon : ICON_TERMINAL));
  lv_obj_set_style_text_color(pg.icon, col(c), 0);
  lv_obj_set_style_text_color(pg.state, col(c), 0);
  lv_obj_set_style_arc_color(pg.ring, col(c), LV_PART_INDICATOR);
}

void clear_progress() { pg.active = false; }

void notify(const char *text, Level level, uint32_t seconds, uint32_t now) {
  static const uint32_t colour[] = {C_RUN, C_OK, C_WARN, C_FAIL};
  static const char *const icon[] = {ICON_INFO, ICON_CHECK, ICON_WARNING, ICON_TIMES};
  int i = (int)level;
  lv_obj_set_style_text_color(nt.icon, col(colour[i]), 0);
  lv_obj_set_style_arc_color(nt.ring, col(colour[i]), LV_PART_INDICATOR);
  set_text(nt.icon, icon[i]);
  char buf[160];
  copy_utf8(buf, text, sizeof buf);
  lv_obj_set_style_text_font(nt.text, strlen(buf) <= 28 ? &w6_text_24 : &w6_text_18, 0);
  lv_label_set_text(nt.text, buf);
  nt.active = true;
  nt.until = now + seconds * 1000;
}

void clear_notify() { nt.active = false; }

void pair_screen(const char *payload, const char *code, uint32_t seconds, uint32_t now) {
  lv_qrcode_update(pr.qr, payload, strlen(payload));
  set_text(pr.code, code);
  pr.total = seconds * 1000, pr.until = now + pr.total, pr.active = true;
}

void clear_pair() { pr.active = false; }

void home() { base = Base::Clock; }

void setup_screen(const char *ap_ssid, const char *ap_pass) {
  char payload[128];
  snprintf(payload, sizeof payload, "WIFI:T:WPA;S:%s;P:%s;;", ap_ssid, ap_pass);
  lv_qrcode_update(su.qr, payload, strlen(payload));
  lv_label_set_text(su.ssid, ap_ssid);
  lv_label_set_text_fmt(su.pass, "password  %s", ap_pass);
  base = Base::Setup;
}

void message(const char *icon, const char *line1, const char *line2) {
  set_text(ms.icon, icon ? icon : "");
  set_text(ms.line1, line1 ? line1 : "");
  set_text(ms.line2, line2 ? line2 : "");
  base = Base::Message;
}

static void fmt_elapsed(char *buf, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s >= 3600) snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                          (unsigned long)(s % 60));
  else snprintf(buf, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static void update_progress(uint32_t now) {
  char buf[48], el[16];
  bool running = pg.st == State::Running;
  int32_t ms = (int32_t)((running ? now : pg.finished) - pg.started);
  fmt_elapsed(el, sizeof el, ms > 0 ? ms : 0);
  if (pg.percent < 0 && running) {  // unknown total: a quarter ring spins, the centre shows the elapsed time
    int a = (int)(now % 1500 * 360 / 1500);
    set_arc(pg.ring, a, a + 90);
    set_text(pg.pct, el);
    set_text(pg.state, "Running");
    return;
  }
  float target = pg.percent < 0 ? 100 : pg.percent;
  pg.shown += (target - pg.shown) * 0.15f;
  if (target - pg.shown < 0.2f && target - pg.shown > -0.2f) pg.shown = target;
  int deg = (int)(pg.shown * 3.6f + 0.5f);
  set_arc(pg.ring, 0, deg >= 360 ? 359 : deg);
  if (pg.percent < 0) set_text(pg.pct, el);
  else {
    snprintf(buf, sizeof buf, "%d%%", pg.percent);
    set_text(pg.pct, buf);
  }
  const char *word = running ? "Running" : pg.st == State::Success ? "Done" : "Failed";
  snprintf(buf, sizeof buf, "%s  \xC2\xB7  %s", word, el);
  set_text(pg.state, buf);
}

// ---- the faces after Analog (see Extra above)
constexpr float RAD = 3.14159265f / 180;
static const uint32_t C_GHOST = 0x2A2A2A;    // unlit dots
static const uint32_t C_CREAM = 0xE9D8C4, C_WALNUT = 0x9C6B3F, C_NIGHT = 0xB3261E, C_TEAL = 0x5CF5D0;
static const uint32_t C_TERM = 0x39FF7A, C_TERM_DIM = 0x1E8F45, C_TERM_AMBER = 0xFFB347, C_TERM_AMBER_DIM = 0x99642A;
static const char *const WD_SHORT[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char *const MO_SHORT[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static uint32_t seg_colour() { return xf.built == Face::SegmentTeal ? C_TEAL : C_ACCENT; }
static uint32_t term_colour(bool dim) {
  bool amber = xf.built == Face::TerminalAmber;
  return dim ? (amber ? C_TERM_AMBER_DIM : C_TERM_DIM) : (amber ? C_TERM_AMBER : C_TERM);
}
static int hour12(int h) { return h % 12 ? h % 12 : 12; }

// Painting, from art's draw callback. Coordinates are screen pixels; parts outside the area being redrawn are skipped.
static lv_draw_ctx_t *dctx;

static void paint_rect(int x, int y, int w, int h, int radius, uint32_t colour, lv_opa_t opa = LV_OPA_COVER) {
  lv_area_t a = {(lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x + w - 1), (lv_coord_t)(y + h - 1)};
  if (!_lv_area_is_on(&a, dctx->clip_area)) return;
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col(colour), d.bg_opa = opa, d.radius = radius;
  lv_draw_rect(dctx, &d, &a);
}

// The C3 has no floating-point unit, and every small redraw (a hand moving, a dot lighting) runs the whole
// callback: so the 60 positions round the edge come from a table, and a strip that doesn't reach the edge skips them.
static int16_t SIN60[60];  // sin(i * 6 degrees) * 4096
static bool reaches(int r) {  // the area being redrawn has a point at least r px from the centre
  const lv_area_t *a = dctx->clip_area;
  int dx = LV_MAX(LV_ABS(a->x1 - 120), LV_ABS(a->x2 - 120)), dy = LV_MAX(LV_ABS(a->y1 - 120), LV_ABS(a->y2 - 120));
  return dx * dx + dy * dy >= r * r;
}

static lv_area_t dot_at(int i, int r, int d) {  // a d px dot r px from the centre, at minute i (0-59) of a clock face
  if (!SIN60[15])
    for (int k = 0; k < 60; k++) SIN60[k] = (int16_t)lroundf(4096 * sinf(k * 6 * RAD));
  int x = 120 + ((r * SIN60[i % 60] + 2048) >> 12) - d / 2, y = 120 - ((r * SIN60[(i + 15) % 60] + 2048) >> 12) - d / 2;
  return {(lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x + d - 1), (lv_coord_t)(y + d - 1)};
}

static lv_area_t dot_area(float deg, int r, int d) {  // the same at any angle, deg clockwise from 12
  int x = 120 + (int)lroundf(r * sinf(deg * RAD)) - d / 2, y = 120 - (int)lroundf(r * cosf(deg * RAD)) - d / 2;
  return {(lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x + d - 1), (lv_coord_t)(y + d - 1)};
}

static void paint_dot(int i, int r, int d, uint32_t colour) {  // at minute i
  lv_area_t a = dot_at(i, r, d);
  paint_rect(a.x1, a.y1, d, d, LV_RADIUS_CIRCLE, colour);
}

static void paint_spoke(float deg, int r0, int r1, int width, uint32_t colour) {  // a line along a radius
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = col(colour), d.width = width, d.round_start = d.round_end = 1;
  float s = sinf(deg * RAD), k = cosf(deg * RAD);
  lv_point_t p1 = {(lv_coord_t)lroundf(120 + r0 * s), (lv_coord_t)lroundf(120 - r0 * k)};
  lv_point_t p2 = {(lv_coord_t)lroundf(120 + r1 * s), (lv_coord_t)lroundf(120 - r1 * k)};
  lv_draw_line(dctx, &d, &p1, &p2);
}

// A seven-segment digit (0-9, or -1 for none) with its unlit segments faintly showing, like a vacuum-fluorescent tube
static const uint8_t SEG7[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};  // bits a..g
static void paint_seven(int x, int y, int w, int h, int t, int digit, uint32_t colour) {
  int g = 2, hh = h / 2, side = hh - 2 * g;  // gap between segments, half height, upright segment length
  const int r[7][4] = {
      {x + t / 2 + g, y, w - t - 2 * g, t},           {x + w - t, y + t / 2 + g, t, side},  // a, b
      {x + w - t, y + hh + g, t, side},               {x + t / 2 + g, y + h - t, w - t - 2 * g, t},  // c, d
      {x, y + hh + g, t, side},                       {x, y + t / 2 + g, t, side},          // e, f
      {x + t / 2 + g, y + hh - t / 2, w - t - 2 * g, t},                                     // g
  };
  uint8_t on = digit < 0 ? 0 : SEG7[digit % 10];
  for (int i = 0; i < 7; i++) paint_rect(r[i][0], r[i][1], r[i][2], r[i][3], t / 2, colour, (on >> i) & 1 ? LV_OPA_COVER : 18);
}

// 5 x 7 dot-matrix digits; index 10 is the colon (one column wide), -1 an unlit cell
static const uint8_t MATRIX[11][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},   {14, 17, 1, 2, 4, 8, 31},  {31, 2, 4, 2, 1, 17, 14},
    {2, 6, 10, 18, 31, 2, 2},     {31, 16, 30, 1, 1, 17, 14}, {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
    {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}, {0, 1, 0, 0, 0, 1, 0}};
constexpr int PIX = 6, MATRIX_X = 45, MATRIX_Y = 68;  // the time: 25 columns of 6 px, centred
static void paint_matrix(int x, int y, int ch, uint32_t colour) {
  int w = ch == 10 ? 1 : 5;
  for (int r = 0; r < 7; r++)
    for (int k = 0; k < w; k++) {
      bool on = ch >= 0 && (MATRIX[ch][r] >> (w - 1 - k)) & 1;
      paint_rect(x + k * PIX, y + r * PIX, PIX - 1, PIX - 1, 1, colour, on ? LV_OPA_COVER : 22);
    }
}

// The four digits of the time: tens of hours (-1 for none: a 12-hour clock before 10), hours, tens of minutes, minutes
static void digits(const Clock &c, int d[4]) {
  if (!c.valid) {
    d[0] = d[1] = d[2] = d[3] = -1;
    return;
  }
  int h = h24 ? c.hour : hour12(c.hour);
  d[0] = h >= 10 || h24 ? h / 10 : -1, d[1] = h % 10, d[2] = c.minute / 10, d[3] = c.minute % 10;
}

// Segment layout: four 36 x 70 digits and a colon across the middle, the seconds at half size underneath
constexpr int SEG_W = 36, SEG_H = 70, SEG_T = 8, SEG_GAP = 9, SEG_COLON = 16, SEG_X = 120 - (4 * SEG_W + 3 * SEG_GAP + SEG_COLON) / 2;
constexpr int SEG_Y = 120 - SEG_H / 2 - 6, SEC_W = 18, SEC_H = 32, SEC_X = 120 - (2 * SEC_W + 5) / 2, SEC_Y = 168;

// ---- living faces: living_fx.cpp paints their backgrounds
static bool living(Face f) { return f >= Face::Wormhole && f <= Face::Grain; }
static lfx::Kind lkind(Face f) { return (lfx::Kind)((int)f - (int)Face::Wormhole); }
uint32_t living_frames() { return lfx::frames() + chfx::frames(); }

static void paint_art(lv_event_t *e) {
  dctx = lv_event_get_draw_ctx(e);
  if (living(xf.built)) return lfx::paint(dctx);
  const Clock &c = xf.c;
  int d[4];
  switch (xf.built) {
    case Face::StackCalm:
      if (reaches(110))
        for (int i = 0; i < 60; i++) paint_dot(i, 112, 3, c.valid && i <= c.second ? C_WALNUT : C_GHOST);
      break;
    case Face::Orbit:
    case Face::OrbitNight: {
      bool night = xf.built == Face::OrbitNight;
      for (int i = 0; i < 60 && reaches(106); i++) {
        bool hour = i % 5 == 0, lit = c.valid && i < c.minute;
        uint32_t off = hour ? (night ? 0x3A0E0A : 0x4A4A4A) : (night ? 0x200806 : 0x262626);
        paint_dot(i, 110, hour ? 7 : 4, lit ? (night ? C_NIGHT : C_ACCENT) : off);
      }
      break;
    }
    case Face::Segment:
    case Face::SegmentTeal: {
      uint32_t k = seg_colour();
      digits(c, d);
      for (int i = 0, x = SEG_X; i < 4; i++, x += SEG_W + SEG_GAP) {
        paint_seven(x, SEG_Y, SEG_W, SEG_H, SEG_T, d[i], k);
        if (i == 1) {
          int cx = x + SEG_W + SEG_GAP + SEG_COLON / 2 - 4 - SEG_GAP / 2;
          paint_rect(cx, SEG_Y + SEG_H / 3 - 4, 8, 8, 2, k);
          paint_rect(cx, SEG_Y + SEG_H * 2 / 3 - 4, 8, 8, 2, k);
          x += SEG_COLON;
        }
      }
      paint_seven(SEC_X, SEC_Y, SEC_W, SEC_H, 4, c.valid ? c.second / 10 : -1, k);
      paint_seven(SEC_X + SEC_W + 5, SEC_Y, SEC_W, SEC_H, 4, c.valid ? c.second % 10 : -1, k);
      break;
    }
    case Face::Words:
      if (!reaches(107)) break;
      for (int i = 0; i < 60; i += 5) paint_dot(i, 112, 3, 0x3A3A3A);
      if (c.valid) {
        lv_area_t a = dot_area((c.hour % 12 * 60 + c.minute) * 0.5f, 112, 8);
        paint_rect(a.x1, a.y1, 8, 8, LV_RADIUS_CIRCLE, C_ACCENT);
      }
      break;
    case Face::Walnut:
      for (int i = 0; i < 60 && reaches(101); i++)
        if (i % 5) paint_dot(i, 111, 3, 0x4A4A4A);
        else if (i % 15) paint_spoke(i * 6, 104, 114, 3, C_DIM);
      break;
    case Face::Minimal:
      for (int i = 0; i < 4 && reaches(96); i++) paint_spoke(i * 90, 100, 112, 6, C_DIM);
      break;
    case Face::Terminal:
    case Face::TerminalAmber: {
      digits(c, d);
      int ch[5] = {d[0], d[1], 10, d[2], d[3]};
      for (int i = 0, x = MATRIX_X; i < 5; x += (ch[i] == 10 ? 1 : 5) * PIX + PIX, i++) paint_matrix(x, MATRIX_Y, ch[i], term_colour(false));
      break;
    }
    default:
      break;
  }
}

// ---- building
static lv_obj_t *text_at(const lv_font_t *font, uint32_t colour, int x, int y) {
  lv_obj_t *l = label(xf.scr, font, colour, y);
  lv_obj_align(l, LV_ALIGN_CENTER, x, y);
  return l;
}

static lv_obj_t *term_line(uint32_t colour, int y) {  // left-aligned, as in a terminal
  lv_obj_t *l = lv_label_create(xf.scr);
  lv_label_set_text(l, "");
  lv_obj_set_style_text_font(l, &lv_font_unscii_16, 0);
  lv_obj_set_style_text_color(l, col(colour), 0);
  lv_obj_set_pos(l, 40, 120 + y);
  return l;
}

static lv_obj_t *round_dot(int d, uint32_t colour) {  // at the centre
  lv_obj_t *o = lv_obj_create(xf.scr);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, col(colour), 0);
  lv_obj_center(o);
  return o;
}

// The time and date on a living face, each over a soft drop shadow so they read on any colour
static void living_text(const lv_font_t *tf, uint32_t tc, int ty, const lv_font_t *df, uint32_t dc_, int dy, bool date_shadow) {
  xf.time_sh = text_at(tf, 0x000000, 2, ty + 2);
  lv_obj_set_style_text_opa(xf.time_sh, 140, 0);
  xf.time = text_at(tf, tc, 0, ty);
  if (date_shadow) {
    xf.date_sh = text_at(df, 0x000000, 1, dy + 1);
    lv_obj_set_style_text_opa(xf.date_sh, 140, 0);
  }
  xf.date = text_at(df, dc_, 0, dy);
}

static void build_extra(Face f) {
  xf = Extra();
  xf.built = f;
  xf.scr = new_screen();
  xf.art = lv_obj_create(xf.scr);
  lv_obj_remove_style_all(xf.art);
  lv_obj_set_size(xf.art, 240, 240);
  lv_obj_clear_flag(xf.art, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(xf.art, paint_art, LV_EVENT_DRAW_MAIN, nullptr);
  switch (f) {
    case Face::Stack:
      xf.a = text_at(&w6_clock_68, C_TEXT, 0, -34);
      xf.b = text_at(&w6_clock_68, C_ACCENT, 0, 34);
      xf.date = text_at(&w6_text_14, C_DIM, -84, -10);
      xf.day = text_at(&w6_text_24, C_TEXT, -84, 12);
      xf.month = text_at(&w6_text_14, C_DIM, 84, -10);
      xf.ampm = text_at(&w6_text_24, C_TEXT, 84, 12);
      break;
    case Face::StackCalm:
      xf.a = text_at(&w6_clock_68, C_CREAM, 0, -34);
      xf.b = text_at(&w6_clock_68, C_WALNUT, 0, 34);
      break;
    case Face::Orbit:
    case Face::OrbitNight: {
      bool night = f == Face::OrbitNight;
      xf.time = text_at(&w6_num_50, night ? 0xC0392B : C_TEXT, 0, -8);
      xf.date = text_at(&w6_text_14, night ? 0x7A1A14 : C_DIM, 0, 30);
      break;
    }
    case Face::Segment:
    case Face::SegmentTeal:
      xf.date = text_at(&w6_text_14, seg_colour(), 0, -66);
      xf.ampm = text_at(&w6_text_14, seg_colour(), 70, 52);
      break;
    case Face::Words:
      xf.a = text_at(&w6_text_18, C_DIM, 0, -70);        // "nearly", "just after", "it's"
      xf.b = text_at(&w6_words_36, C_TEXT, 0, -34);      // "quarter"
      xf.info = text_at(&w6_text_24, C_DIM, 0, 2);       // "to", "past", "o'clock"
      xf.time = text_at(&w6_words_48, C_ACCENT, 0, 42);  // "eight"
      break;
    case Face::Walnut: {
      lv_label_set_text(text_at(&w6_text_24, C_TEXT, 0, -94), "12");
      lv_label_set_text(text_at(&w6_text_24, C_TEXT, -98, 0), "9");
      lv_label_set_text(text_at(&w6_text_24, C_TEXT, 0, 94), "6");
      lv_obj_t *win = lv_obj_create(xf.scr);  // the date window, where the 3 would be
      lv_obj_remove_style_all(win);
      lv_obj_set_size(win, 30, 26);
      lv_obj_align(win, LV_ALIGN_CENTER, 91, 0);
      lv_obj_set_style_radius(win, 4, 0);
      lv_obj_set_style_border_width(win, 1, 0);
      lv_obj_set_style_border_color(win, col(C_ACCENT), 0);
      xf.day = lv_label_create(win);
      lv_obj_set_style_text_font(xf.day, &w6_text_18, 0);
      lv_obj_set_style_text_color(xf.day, col(C_ACCENT), 0);
      lv_obj_center(xf.day);
      xf.hand[0] = hand(xf.scr, xf.pts[0], 8, C_CREAM);
      xf.hand[1] = hand(xf.scr, xf.pts[1], 5, C_TEXT);
      xf.hand[2] = hand(xf.scr, xf.pts[2], 2, C_FAIL);
      round_dot(14, C_ACCENT);
      round_dot(4, 0x000000);
      break;
    }
    case Face::Minimal:
      xf.ring = ring(xf.scr, 232, 4, 0x3A2A1C);
      lv_obj_set_style_arc_color(xf.ring, col(C_ACCENT), LV_PART_INDICATOR);
      xf.date = text_at(&w6_text_14, C_DIM, 0, 56);
      xf.hand[0] = hand(xf.scr, xf.pts[0], 12, C_ACCENT);
      xf.hand[1] = hand(xf.scr, xf.pts[1], 6, C_TEXT);
      round_dot(16, C_TEXT);
      break;
    case Face::Terminal:
    case Face::TerminalAmber:
      xf.cmd = term_line(term_colour(true), -84);
      xf.date = term_line(term_colour(false), 4);
      xf.job = term_line(term_colour(true), 28);
      xf.result = term_line(term_colour(false), 48);
      lv_label_set_text(term_line(term_colour(true), 72), "$");
      xf.cursor = lv_obj_create(xf.scr);
      lv_obj_remove_style_all(xf.cursor);
      lv_obj_set_size(xf.cursor, 8, 15);
      lv_obj_set_pos(xf.cursor, 56, 192);
      lv_obj_set_style_bg_opa(xf.cursor, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(xf.cursor, col(term_colour(false)), 0);
      break;
    case Face::Wormhole:
      living_text(&w6_clock_68, 0xFFFFFF, -6, &w6_text_14, 0xC9C2FF, 34, false);
      break;
    case Face::Aurora:
      living_text(&w6_clock_68, 0xFFFFFF, -10, &w6_text_18, 0xE8E0FF, 36, true);
      break;
    case Face::Galaxy:
      living_text(&w6_num_50, 0xFFFFFF, -64, &w6_text_14, 0x9FA8D9, 96, false);
      break;
    case Face::Silk:
      living_text(&w6_clock_68, 0xFFF4E8, -10, &w6_text_18, 0xFFD9B8, 36, true);
      break;
    case Face::Sky:  // no shadows: the colours change between day and night instead (update_extra)
      xf.time = text_at(&w6_clock_68, C_TEXT, 0, -38);
      xf.date = text_at(&w6_text_14, C_TEXT, 0, 2);
      break;
    case Face::Grain:
      for (int i = 0; i < 3; i++) {  // shadows first, so the hands sit on them
        xf.shadow[i] = hand(xf.scr, xf.spts[i], i == 0 ? 9 : i == 1 ? 6 : 2, 0x000000);
        lv_obj_set_pos(xf.shadow[i], 3, 4);
        lv_obj_set_style_line_opa(xf.shadow[i], i == 2 ? 70 : 90, 0);
      }
      xf.hand[0] = hand(xf.scr, xf.pts[0], 9, 0xF3E3C8);
      xf.hand[1] = hand(xf.scr, xf.pts[1], 6, 0xFBF1E0);
      xf.hand[2] = hand(xf.scr, xf.pts[2], 2, 0xE0B060);
      round_dot(12, 0xE6B96E);
      break;
    default:
      break;
  }
  if (living(f)) {  // the tables, for as long as the face is on
    lfx::begin(lkind(f));  // without the memory, the time shows on black until a retry works (update_extra)
  }
  xf.nowifi = text_at(&w6_icons_18, C_FAIL, 0, 104);
  lv_label_set_text(xf.nowifi, ICON_WIFI);
  show_obj(xf.nowifi, !online);
}

// Builds face f if it is one of these, and deletes the one that was built before
static void use_extra(Face f) {
  if (xf.scr && xf.built == f) return;
  if (xf.scr) {
    if (current == xf.scr) current = ck.scr, lv_scr_load(ck.scr);
    if (fading == xf.scr) fading = nullptr;
    lv_obj_del(xf.scr);
    xf = Extra();
    lfx::end();
  }
  if (f >= Face::Stack && f < Face::Count) build_extra(f);
}

// ---- updating
static void words(const Clock &c) {  // "nearly" / "ten" / "past" / "ten"
  static const char *const NUM[] = {"twelve", "one", "two",   "three", "four",   "five",
                                    "six",    "seven", "eight", "nine", "ten", "eleven"};
  if (!c.valid) {
    set_text(xf.a, ""), set_text(xf.b, ""), set_text(xf.info, "--:--"), set_text(xf.time, "");
    lv_obj_align(xf.info, LV_ALIGN_CENTER, 0, 0);
    return;
  }
  int r = (c.minute + 2) / 5 * 5, diff = c.minute - r, h = c.hour;
  static const char *const QTY[] = {"", "five", "ten", "quarter", "twenty", "twenty-five", "half"};
  const char *qty = r == 0 || r == 60 ? "" : QTY[r <= 30 ? r / 5 : (60 - r) / 5];
  if (r > 30) h++;
  set_text(xf.a, diff < 0 ? "nearly" : diff > 0 ? "just after" : "it's");
  set_text(xf.b, qty);
  set_text(xf.info, !*qty ? "o'clock" : r > 30 ? "to" : "past");
  set_text(xf.time, NUM[h % 12]);
  lv_obj_align(xf.info, LV_ALIGN_CENTER, 0, *qty ? 2 : 22);
  lv_obj_align(xf.time, LV_ALIGN_CENTER, 0, *qty ? 42 : -22);
  // "twenty-five" is wider than the circle at that height in the 36 px font; it takes the 24 px one
  lv_obj_set_style_text_font(xf.b, strlen(qty) > 8 ? &w6_text_24 : &w6_words_36, 0);
}

static void point_hands(const Clock &c, int hour_len, int min_len, bool seconds) {
  if (!c.valid) {
    for (int i = 0; i < 3; i++)
      if (xf.hand[i]) point_hand(xf.hand[i], xf.pts[i], 0, 0, 0);
    return;
  }
  point_hand(xf.hand[0], xf.pts[0], (c.hour % 12) * 30 + c.minute * 0.5f, hour_len, 10);
  point_hand(xf.hand[1], xf.pts[1], c.minute * 6 + (seconds ? c.second * 0.1f : c.second / 10 * 1.0f), min_len, 12);
  if (seconds) point_hand(xf.hand[2], xf.pts[2], c.second * 6, 100, 20);
}

static void ago(char *buf, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60) snprintf(buf, n, "now");
  else if (s < 3600) snprintf(buf, n, "%lum ago", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(buf, n, "%luh ago", (unsigned long)(s / 3600));
  else snprintf(buf, n, "%lud ago", (unsigned long)(s / 86400));
}

static void terminal_job(uint32_t now) {  // the last progress job, or the uptime before there has been one
  char cmd[16] = "$ ", res[24];
  size_t k = 2;
  for (const char *p = pg.title_txt; *p >= 0x20 && *p < 0x7F && k < 11; p++) cmd[k++] = *p;  // stops at "·" etc.
  while (k > 2 && cmd[k - 1] == ' ') k--;
  cmd[k] = 0;
  uint32_t colour = term_colour(false);
  if (k == 2) {  // no job yet
    uint32_t m = now / 60000;
    snprintf(cmd, sizeof cmd, "$ uptime");
    if (m >= 1440) snprintf(res, sizeof res, "up %lud %luh", (unsigned long)(m / 1440), (unsigned long)(m / 60 % 24));
    else snprintf(res, sizeof res, "up %luh %lum", (unsigned long)(m / 60), (unsigned long)(m % 60));
  } else if (pg.st == State::Running) {
    char el[16];
    fmt_elapsed(el, sizeof el, now - pg.started);
    if (pg.percent >= 0) snprintf(res, sizeof res, "RUN %d%%", pg.percent);
    else snprintf(res, sizeof res, "RUN %s", el);
  } else {
    char a[16];
    ago(a, sizeof a, now - pg.finished);
    bool ok = pg.st == State::Success;
    snprintf(res, sizeof res, "%s %s", ok ? "OK" : "FAIL", a);
    if (!ok) colour = C_FAIL;
  }
  set_text(xf.job, cmd);
  set_text(xf.result, res);
  if (lv_obj_get_style_text_color(xf.result, LV_PART_MAIN).full != col(colour).full)
    lv_obj_set_style_text_color(xf.result, col(colour), 0);
}

static bool f_is_terminal(Face f) { return f == Face::Terminal || f == Face::TerminalAmber; }

static void update_extra(uint32_t now, const Clock &c) {
  const Clock was = xf.c;
  bool minute = !xf.drawn || xf.dirty || c.valid != was.valid || c.minute != was.minute || c.hour != was.hour ||
                c.day != was.day;
  bool second = minute || c.second != was.second;
  xf.c = c;
  if (f_is_terminal(xf.built) && now / 1000 != xf.job_s) xf.job_s = now / 1000, terminal_job(now);
  if (!xf.drawn) xf.t0 = now;
  // No memory for the tables when the face was built (the time shows on black): try again each second, as memory
  // comes back when a request or album art lets go of it
  if (living(xf.built) && !lfx::active() && now - xf.tried_at >= 1000) {
    xf.tried_at = now;
    if (lfx::begin(lkind(xf.built))) {
      lfx::minute(c.valid ? c.hour * 60 + c.minute : 12 * 60);
      xf.frame_at = now - 1000;
      lv_obj_invalidate(xf.art);
    }
  }
  if (lfx::active() && living(xf.built) && lfx::moving(lkind(xf.built))) {  // the next frame
    // 12 a second for the galaxy, 10 for the rest: a tunnel frame costs ~60 ms, and the web server and the app's
    // screen picture need time between frames
    uint32_t every = xf.built == Face::Galaxy ? 83 : 100;
    if (!xf.drawn || now - xf.frame_at >= every) {
      xf.frame_at = now;
      lfx::frame((now - xf.t0) % (6 * 3600000u));  // wraps every 6 h, so float maths on it stays precise
      lv_obj_invalidate(xf.art);
    }
  }
  if (!minute && !second) return;
  char buf[40];
  Face f = xf.built;
  if (minute) {
    xf.dirty = false, xf.drawn = true;
    if (f != Face::Grain) lv_obj_invalidate(xf.art);  // the wood never changes; only the hands' strips are redrawn
    const char *wd = WD_SHORT[c.weekday % 7], *mo = MO_SHORT[(c.month + 11) % 12];
    switch (f) {
      case Face::Stack:
      case Face::StackCalm:
        if (c.valid) snprintf(buf, sizeof buf, "%02d", h24 ? c.hour : hour12(c.hour));
        set_text(xf.a, c.valid ? buf : "--");
        if (c.valid) snprintf(buf, sizeof buf, "%02d", c.minute);
        set_text(xf.b, c.valid ? buf : "--");
        if (f == Face::StackCalm) break;
        set_text(xf.date, c.valid ? wd : "");
        snprintf(buf, sizeof buf, "%d", c.day);
        set_text(xf.day, c.valid ? buf : "");
        set_text(xf.month, c.valid ? mo : "");
        lv_obj_align(xf.month, LV_ALIGN_CENTER, 84, h24 ? 0 : -10);  // alone in 24-hour time: level with the middle
        set_text(xf.ampm, c.valid && !h24 ? (c.hour < 12 ? "AM" : "PM") : "");
        break;
      case Face::Orbit:
      case Face::OrbitNight:
        if (c.valid) hhmm(buf, sizeof buf, c.hour * 60 + c.minute);
        if (c.valid && !h24) *strchr(buf, ' ') = 0;  // the 50 px digits have no letters: "7:45", no PM
        set_text(xf.time, c.valid ? buf : "--:--");
        snprintf(buf, sizeof buf, "%s %d %s", wd, c.day, mo);
        set_text(xf.date, c.valid ? buf : "");
        break;
      case Face::Segment:
      case Face::SegmentTeal:
        snprintf(buf, sizeof buf, "%s %d %s", wd, c.day, mo);
        set_text(xf.date, c.valid ? buf : "");
        set_text(xf.ampm, c.valid && !h24 ? (c.hour < 12 ? "AM" : "PM") : "");
        break;
      case Face::Words:
        words(c);
        break;
      case Face::Walnut:
        snprintf(buf, sizeof buf, "%d", c.day);
        set_text(xf.day, c.valid ? buf : "");
        break;
      case Face::Minimal:
        snprintf(buf, sizeof buf, "%s %d", wd, c.day);
        set_text(xf.date, c.valid ? buf : "");
        break;
      case Face::Terminal:
      case Face::TerminalAmber: {
        set_text(xf.cmd, h24 ? "$ date +%R" : "$ date");
        static const char *const WDM[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        static const char *const MOM[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        snprintf(buf, sizeof buf, "%s %d %s", WDM[c.weekday % 7], c.day, MOM[(c.month + 11) % 12]);  // no room for PM
        set_text(xf.date, c.valid ? buf : "");
        break;
      }
      case Face::Wormhole:
      case Face::Aurora:
      case Face::Galaxy:
      case Face::Silk:
      case Face::Sky: {
        if (c.valid) snprintf(buf, sizeof buf, "%d:%02d", h24 ? c.hour : hour12(c.hour), c.minute);
        set_text(xf.time, c.valid ? buf : "--:--");
        if (xf.time_sh) set_text(xf.time_sh, c.valid ? buf : "--:--");
        snprintf(buf, sizeof buf, f == Face::Aurora || f == Face::Silk ? "%s  %d %s" : "%s %d %s", wd, c.day, mo);
        set_text(xf.date, c.valid ? buf : "");
        if (xf.date_sh) set_text(xf.date_sh, c.valid ? buf : "");
        int mod = c.valid ? c.hour * 60 + c.minute : 12 * 60;
        lfx::minute(mod);
        if (f == Face::Sky) {
          bool light = mod > 7 * 60 + 30 && mod < 17 * 60;  // dark digits on a bright sky
          lv_obj_set_style_text_color(xf.time, col(light ? 0x10284A : 0xFFFFFF), 0);
          lv_obj_set_style_text_color(xf.date, col(light ? 0x1E3E66 : 0xC9D6FF), 0);
        }
        break;
      }
      default:
        break;
    }
  }
  if (!second) return;
  switch (f) {
    case Face::StackCalm:  // the next dot lights; a new minute redraws them all (above)
      if (!minute && c.valid) {
        lv_area_t a = dot_at(c.second, 112, 3);
        lv_obj_invalidate_area(xf.art, &a);
      }
      break;
    case Face::Segment:
    case Face::SegmentTeal:
      if (!minute) {
        lv_area_t a = {SEC_X, SEC_Y, SEC_X + 2 * SEC_W + 5, SEC_Y + SEC_H};
        lv_obj_invalidate_area(xf.art, &a);
      }
      break;
    case Face::Walnut:
      point_hands(c, 58, 92, true);
      break;
    case Face::Grain:
      point_hands(c, 56, 90, true);
      for (int i = 0; i < 3; i++) {
        memcpy(xf.spts[i], xf.pts[i], sizeof xf.pts[i]);
        lv_line_set_points(xf.shadow[i], xf.spts[i], 2);
      }
      break;
    case Face::Minimal:  // moves every 10 s: the hands and the track creep, without a redraw every second
      if (minute || c.second % 10 == 0) {
        point_hands(c, 50, 94, false);
        int deg = c.valid ? c.minute * 6 + c.second / 10 : 0;
        set_arc(xf.ring, 0, deg ? deg : 1);
      }
      break;
    case Face::Terminal:
    case Face::TerminalAmber:
      show_obj(xf.cursor, c.second % 2 == 0);
      break;
    default:
      break;
  }
}

void tick(uint32_t now, const Clock &c) {
  if (nt.active && (int32_t)(now - nt.until) >= 0) nt.active = false;
  if (pr.active && (int32_t)(now - pr.until) >= 0) pr.active = false;
  if (pg.active && (int32_t)(now - pg.updated) >= (int32_t)pg.ttl_ms) pg.active = false;  // signed: now may be a
  // little older than an update that arrived during this loop
  if (ph.active && (int32_t)(now - ph.updated) >= (int32_t)ph.ttl_ms) clear_phone();
  if (md.active && (int32_t)(now - md.updated) >= (int32_t)md.ttl_ms) clear_media();
  if (md.active && !md.m.playing && art_px && now - md.paused_at >= PAUSED_SHOW_MS) free_art();  // paused and gone
  bool charging = ph.active && ph.p.charging;  // a phone that isn't charging doesn't take over the screen

  lv_obj_t *home_scr = face == Face::Analog ? an.scr : xf.scr ? xf.scr : ck.scr;
  bool at_420 = c.valid && c.hour == 16 && c.minute == 20;
  if (lv.active && (lv.daily ? !at_420 : now - lv.started >= FUN_MS)) lv.active = false;
  lv_obj_t *want = pr.active ? pr.scr
                   : nt.active ? nt.scr
                   : tm.active ? tm.scr
                   : pg.active ? pg.scr
                   : base == Base::Setup ? su.scr
                   : media_showing(now) && base == Base::Clock ? md.scr
                   : charging && base == Base::Clock ? ph.scr
                   : base == Base::Message ? ms.scr
                   : home_scr;
  // 16:20: once a day, as soon as the clock is showing during that minute (a notification or music goes first)
  if (want == home_scr && at_420 && lv.played_day != c.day) {
    lv.played_day = c.day;
    fun(now);
    lv.daily = true;
  }
  if (want == home_scr && lv.active) want = lv.scr;
  if (want == lv.scr) update_lava(now);
  if (want == ck.scr) update_clock(c);
  if (want == an.scr) update_analog(c);
  if (want == xf.scr) update_extra(now, c);
  if (want == tm.scr) update_timer(now, c);
  if (want == pg.scr) update_progress(now);
  if (want == pr.scr) {
    int32_t left = (int32_t)(pr.until - now);
    int deg = left > 0 ? (int)((int64_t)left * 360 / pr.total) : 0;
    set_arc(pr.ring, 0, deg >= 360 ? 359 : deg);
  }
  if (want == md.scr) update_media(now, c);
  if (want == ph.scr) {
    if (current != ph.scr) {  // sweep up from empty each time the screen appears
      ph.shown = 0, ph.t0 = now, ph.full_at = -1, ph.fx_tried = false;
      ph.frame_at = now - 1000;
    }
    if (ph.art && !chfx::active() && !ph.fx_tried) {  // Warp and Swarm: their tables, while the screen is on
      ph.fx_tried = true;
      chfx::begin((chfx::Style)ph.style);
    }
    update_charging(now);
    if (!c.valid) {
      set_text(ph.time, ""), ph.shown_min = -1;
      if (ph.time_sh) set_text(ph.time_sh, "");
    } else if (c.minute != ph.shown_min) {
      char buf[12];
      hhmm(buf, sizeof buf, c.hour * 60 + c.minute);
      set_text(ph.time, buf), ph.shown_min = c.minute;
      if (ph.time_sh) set_text(ph.time_sh, buf);
    }
  }
  // Switching between the clock and the charging screen fades the new one in from black. Done here rather than with
  // lv_scr_load_anim, which crashed in LVGL 8.3.11 when a second switch arrived during a fade.
  static uint32_t fade_from = 0;
  constexpr uint32_t FADE_MS = 350;
  if (want != current) {
    auto calm = [](lv_obj_t *o) {
      return o == ck.scr || o == an.scr || (xf.scr && o == xf.scr) || o == ph.scr || o == tm.scr || o == md.scr || o == lv.scr;
    };
    bool fade = calm(want) && calm(current);
    if (fading) lv_obj_remove_local_style_prop(fading, LV_STYLE_OPA, 0), fading = nullptr;
    current = want;
    lv_scr_load(current);
    if (fade) {
      fading = current, fade_from = now;
      lv_obj_set_style_opa(fading, LV_OPA_TRANSP, 0);
    }
  }
  if (fading) {
    uint32_t t = now - fade_from;
    if (t >= FADE_MS) lv_obj_remove_local_style_prop(fading, LV_STYLE_OPA, 0), fading = nullptr;
    else lv_obj_set_style_opa(fading, (lv_opa_t)(t * 255 / FADE_MS), 0);
  }
  if (lv.scr && !lv.active && current != lv.scr && fading != lv.scr) lv_obj_del(lv.scr), lv.scr = nullptr;
  if (chfx::active() && current != ph.scr && fading != ph.scr) chfx::end();  // the charging screen has gone
}

}  // namespace ui
