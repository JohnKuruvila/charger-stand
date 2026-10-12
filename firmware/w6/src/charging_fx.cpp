// Warp and Swarm, the animated charging screens. See charging_fx.h.
//   Warp:  flying through a star tunnel towards a portal in the charge colour. The streaks run faster and longer the
//          more power the phone takes; at 100 % it speeds up, flashes white on "arrival", and the stars hang still.
//   Swarm: the percentage is made of particles that buzz round their places (more with more power) and keep leaving
//          for the rim and streaming back in. When the number changes they fly to the new digits; at 100 % they burst
//          outwards and re-form.
#include "charging_fx.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

LV_FONT_DECLARE(w6_clock_68);
LV_FONT_DECLARE(w6_num_50);

namespace chfx {

constexpr int W = 240, Q = 120, NOCT = Q * (Q + 1) / 2;
constexpr float PI = 3.14159265f;
constexpr int NP = 1000;  // Swarm's particles

// ---- colours in the panel's byte order
static inline uint16_t sw(uint16_t c) {
#if LV_COLOR_16_SWAP
  return (uint16_t)(c >> 8 | c << 8);
#else
  return c;
#endif
}
static inline int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smooth(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline uint16_t rgb(int r, int g, int b) {  // native RGB565
  r = clampi(r, 0, 255), g = clampi(g, 0, 255), b = clampi(b, 0, 255);
  return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}
struct RGB { float r, g, b; };
static RGB hexc(uint32_t h) { return {(float)(h >> 16 & 255), (float)(h >> 8 & 255), (float)(h & 255)}; }
static RGB mixc(RGB a, RGB b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
static const float DITHER[4] = {-0.375f, 0.125f, 0.375f, -0.125f};  // 2 x 2 ordered dither, phase (y & 1) * 2 + (x & 1)
static uint16_t rgbd(RGB c, int phase) {  // dithered, panel order
  float d = DITHER[phase];
  return sw(rgb((int)floorf(c.r + d * 8 + 4), (int)floorf(c.g + d * 4 + 2), (int)floorf(c.b + d * 8 + 4)));
}
static RGB charge_colour(float pct) { return pct < 20 ? hexc(0xFFC94D) : hexc(0x52D273); }  // ui.cpp's C_WARN, C_OK

// Adds an 8-bit colour, weighted by w/256, to a pixel in panel order, saturating.
static inline void add_px(uint16_t *p, int r, int g, int b, int w) {
  uint16_t d = sw(*p);
  int R = (d >> 11) + ((r * w + 1024) >> 11), G = ((d >> 5) & 63) + ((g * w + 512) >> 10), B = (d & 31) + ((b * w + 1024) >> 11);
  *p = sw((uint16_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (B > 31 ? 31 : B)));
}

// ---- the tables, allocated while the screen is on
struct Warp {
  uint8_t oang[NOCT], orad[NOCT];  // one eighth of the screen: angle from the vertical (256 = 90 degrees), radius in
  uint16_t tri[Q];                 // half pixels; row j of the octant starts at tri[j]
  uint16_t depth[256];             // tunnel depth by radius: 16384 / r
  uint8_t fog[256], hash[256], hash2[256], lane[256], lane2[256], streak[256], portal[256], sin8[256];
  uint8_t lane2b[256], hz2[256];   // lane2 half a turn on (the second set of streaks); the haze's second phase by radius
  uint8_t s1[256], s2[256], hz1[256], glow[256];  // per frame, by radius: the streaks' and the haze's phases, the portal
  uint16_t pal[4][256];
  int tz, tneb, flash, pw;         // pw: the portal's brightness, 0-256
  float tzf, len;
  uint8_t pc[3];
};
struct Swarm {
  int32_t x[NP], y[NP];     // Q8 pixels
  int16_t vx[NP], vy[NP];   // Q4 pixels a second
  int16_t tx[NP], ty[NP];   // where each one belongs on the digits, Q4
  uint8_t ph[NP];           // its phase round that place
  uint8_t ex[W], hot[256];  // background vignette: (x - centre)^2 / 64; heat from speed
  uint16_t bg[2][512];
  int shown;                // the number the targets spell, -1 none
  int32_t burst_at;         // the full_ms when it burst, -1 before
  float inflow;
  uint8_t cr, cg, cb;
  int dtq;                  // the last frame's length, Q10 seconds, for the streaks
};
struct Tables {
  int16_t sinq[1024];  // sin, Q14, 1024 a turn
  uint8_t span[W];     // first x inside the circle on each row
  union { Warp w; Swarm s; };
};
static Tables *T = nullptr;
static Style cur = Style::Classic;
static uint32_t frame_count = 0;
static uint32_t rng = 0x9E3779B9;
static inline uint32_t rnd() { rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5; return rng; }
static inline float frand() { return (rnd() >> 8) * (1.0f / 16777216.0f); }

bool active() { return T != nullptr; }
Style style() { return cur; }
uint32_t frames() { return frame_count; }

// ---------------------------------------------------------------- Warp
static void warp_init() {
  Warp &w = T->w;
  for (int j = 0; j < Q; j++) {
    w.tri[j] = (uint16_t)(j * (j + 1) / 2);
    for (int k = 0; k <= j; k++) {
      float dx = k + 0.5f, dy = j + 0.5f;
      w.oang[w.tri[j] + k] = (uint8_t)lroundf(atan2f(dx, dy) / (PI / 2) * 256);
      w.orad[w.tri[j] + k] = (uint8_t)fminf(255, lroundf(2 * sqrtf(dx * dx + dy * dy)));
    }
  }
  for (int r = 0; r < 256; r++) {
    float px = fmaxf(r / 2.0f, 2);
    w.depth[r] = (uint16_t)fminf(65535, 16384 / px);
    w.fog[r] = (uint8_t)(255 * smooth(62, 100, px) * (1 - 0.15f * smooth(110, 120, px)));
    float d = fabsf(px - 60);  // the portal's ring: its shape here, its brightness per frame
    w.portal[r] = (uint8_t)(255 * clampf(expf(-d * d / 3.0f) + 0.4f * expf(-d / 6.0f), 0, 1));
    w.sin8[r] = (uint8_t)(128 + 127 * sinf(r * 2 * PI / 256));
  }
  for (int u = 0; u < 256; u++) {
    w.hash[u] = (uint8_t)rnd(), w.hash2[u] = (uint8_t)rnd();
    w.lane[u] = frand() < 0.8f ? (uint8_t)(140 + 115 * frand()) : 0;
    w.lane2[u] = frand() < 0.7f ? (uint8_t)(90 + 140 * frand()) : 0;
  }
  for (int i = 0; i < 256; i++) w.lane2b[i] = w.lane2[(i + 128) & 255], w.hz2[i] = (uint8_t)(77 - (w.depth[i] >> 2));
  RGB stops[4] = {hexc(0x020208), hexc(0x24166A), hexc(0x6A9CFF), hexc(0xF6FAFF)};
  for (int i = 0; i < 256; i++) {
    float k = i / 255.0f * 3;
    int a = (int)k;
    RGB c = a >= 3 ? stops[3] : mixc(stops[a], stops[a + 1], k - a);
    for (int p = 0; p < 4; p++) w.pal[p][i] = rgbd(c, p);
  }
  w.tzf = 0, w.len = -1;
}

static void warp_frame(uint32_t t, uint32_t dt, float pct, float e, int32_t full) {
  Warp &w = T->w;
  float speed = 90 + 520 * e;  // depth units a second
  float boost = 1;
  if (full >= 0) boost = full < 900 ? 1 + 3 * smooth(0, 900, full) : 0.04f + 0.3f * expf(-(full - 900) / 300.0f);
  w.tzf += speed * boost * dt / 1000.0f;
  if (w.tzf > 1e6f) w.tzf -= 1e6f - fmodf(1e6f, 256);  // keeps the phase, and float precision
  w.tz = (int)w.tzf, w.tneb = (int)(w.tzf * 0.25f);
  float len = 18 + 70 * e;
  if (full >= 0 && full < 900) len *= 1 + 3 * smooth(0, 900, full);
  if (full >= 900) len = 10;
  if (fabsf(len - w.len) > 0.5f) {  // the streak's fade from head to tail, rebuilt only when its length changes
    w.len = len;
    for (int p = 0; p < 256; p++) {
      float k = p < len ? 1 - p / len : 0;
      w.streak[p] = (uint8_t)(255 * k * k);
    }
  }
  w.flash = full >= 800 && full < 1800 ? (int)(255 * expf(-(full - 800) / 280.0f)) : 0;
  RGB pc = charge_colour(pct);
  w.pc[0] = (uint8_t)pc.r, w.pc[1] = (uint8_t)pc.g, w.pc[2] = (uint8_t)pc.b;
  float pw = 0.55f + (full < 0 ? 0.25f * sinf(t / 1000.0f * (2 + 4 * e)) : 0);  // the portal pulses with the power
  w.pw = (int)(pw * 256);
  for (int r = 0; r < 256; r++) {  // what each pixel would otherwise work out from its radius
    int D = w.depth[r];
    w.s1[r] = (uint8_t)(D + w.tz), w.s2[r] = (uint8_t)((D * 3 >> 1) + w.tz), w.hz1[r] = (uint8_t)((D >> 1) + w.tneb);
    w.glow[r] = (uint8_t)(w.portal[r] * w.pw >> 8);
  }
}

// One pixel at angle ang (0 at 12 o'clock, 1024 a turn) and radius rad (half pixels); pal: the row's two dither phases
static inline uint16_t warp_px(const Warp &w, int ang, int rad, const uint16_t *pal) {
  int I = 0, f = w.fog[rad];
  if (f) {  // the dark middle (fog 0, a quarter of the screen) has no streaks or haze
    int u = (ang >> 2) & 255;
    int a = w.streak[(w.s1[rad] + w.hash[u]) & 255] * w.lane[u];
    int b = w.streak[(w.s2[rad] + w.hash2[u]) & 255] * w.lane2b[u];
    int m = (a > b ? a : b) >> 8;
    int neb = (w.sin8[(u * 3 + w.hz1[rad]) & 255] * w.sin8[(u * 5 + w.hz2[rad]) & 255]) >> 10;  // violet haze
    I = (m > neb ? m : neb) * f >> 8;
  }
  I += w.flash;
  if (I > 255) I = 255;
  uint16_t c = pal[I];
  int p = w.glow[rad];
  if (p > 8) {  // the portal's light, added
    uint16_t n = sw(c);
    int r = (n >> 11) * 8 + (w.pc[0] * p >> 8), g = ((n >> 5) & 63) * 4 + (w.pc[1] * p >> 8), bl = (n & 31) * 8 + (w.pc[2] * p >> 8);
    c = sw((uint16_t)((r > 255 ? 31 : r >> 3) << 11 | (g > 255 ? 63 : g >> 2) << 5 | (bl > 255 ? 31 : bl >> 3)));
  }
  return c;
}

// Each row in four runs, by the octant its pixels' angle and radius come from, so no pixel has to work that out
static void warp_paint(uint16_t *buf, int bw, const lv_area_t *ba, const lv_area_t *clip) {
  const Warp &w = T->w;
  for (int y = clip->y1; y <= clip->y2; y++) {
    int x0 = T->span[y], x1 = W - 1 - x0;
    if (x0 < clip->x1) x0 = clip->x1;
    if (x1 > clip->x2) x1 = clip->x2;
    uint16_t *row = buf + (y - ba->y1) * bw - ba->x1;
    bool top = y < Q;
    int j = top ? Q - 1 - y : y - Q, tj = w.tri[j];
    const uint16_t *pe = w.pal[(y & 1) << 1], *po = pe + 256;  // even and odd x
    // left half, outside the diagonal (k = Q - 1 - x > j): the octant mirrored
    int base = 768, sgn = top ? 1 : -1, x = x0;
    for (; x <= x1 && x < Q - 1 - j; x++) {
      int i = w.tri[Q - 1 - x] + j;
      row[x] = warp_px(w, base + sgn * w.oang[i], w.orad[i], x & 1 ? po : pe);
    }
    // left half, inside it
    base = top ? 1024 : 512, sgn = top ? -1 : 1;
    for (; x <= x1 && x < Q; x++) {
      int i = tj + Q - 1 - x;
      row[x] = warp_px(w, base + sgn * w.oang[i], w.orad[i], x & 1 ? po : pe);
    }
    // right half, inside the diagonal (k = x - Q <= j)
    base = top ? 0 : 512, sgn = top ? 1 : -1;
    for (; x <= x1 && x <= Q + j; x++) {
      int i = tj + x - Q;
      row[x] = warp_px(w, base + sgn * w.oang[i], w.orad[i], x & 1 ? po : pe);
    }
    // right half, outside it
    base = 256, sgn = top ? -1 : 1;
    for (; x <= x1; x++) {
      int i = w.tri[x - Q] + j;
      row[x] = warp_px(w, base + sgn * w.oang[i], w.orad[i], x & 1 ? po : pe);
    }
  }
}

// ---------------------------------------------------------------- Swarm
// Calls f(x, y) for every pixel of the percentage as Swarm writes it: 68 px digits and a 50 px "%" (the 68 px font
// has no %), read straight from the fonts' 4-bit glyph bitmaps.
template <class F> static void number_pixels(int pct, F f) {
  char d[8];
  int n = 0;
  for (int v = pct < 0 ? 0 : pct > 999 ? 999 : pct;; v /= 10) { d[n++] = (char)('0' + v % 10); if (v < 10) break; }
  const lv_font_t *fd = &w6_clock_68, *fp = &w6_num_50;
  int wd = 0;
  lv_font_glyph_dsc_t g;
  for (int i = n - 1; i >= 0; i--)
    if (lv_font_get_glyph_dsc(fd, &g, (uint8_t)d[i], 0)) wd += g.adv_w;
  int lhd = lv_font_get_line_height(fd), lhp = lv_font_get_line_height(fp);
  int top_d = 104 - lhd / 2, base_d = top_d + lhd - fd->base_line;  // the digits' line centred 16 px above the middle
  int top_p = top_d + lhd - 4 - lhp, base_p = top_p + lhp - fp->base_line;  // "%" bottom-aligned, 4 px up
  int pen = 106 - wd / 2;
  auto glyph = [&](const lv_font_t *font, uint32_t ch, int base) {
    if (!lv_font_get_glyph_dsc(font, &g, ch, 0)) return;
    const uint8_t *bmp = lv_font_get_glyph_bitmap(font, ch);
    if (bmp)
      for (int gy = 0; gy < g.box_h; gy++)
        for (int gx = 0; gx < g.box_w; gx++) {
          int i = gy * g.box_w + gx, a = (i & 1) ? bmp[i >> 1] & 15 : bmp[i >> 1] >> 4;  // 4 bpp, rows not padded
          if (a >= 8) f(pen + g.ofs_x + gx, base - g.box_h - g.ofs_y + gy);
        }
    pen += g.adv_w;
  };
  for (int i = n - 1; i >= 0; i--) glyph(fd, (uint8_t)d[i], base_d);
  pen += 3;
  glyph(fp, '%', base_p);
}

static void swarm_targets(int pct) {
  Swarm &s = T->s;
  int m = 0;
  number_pixels(pct, [&](int, int) { m++; });
  if (m == 0) return;
  int k = 0, next = 0;
  number_pixels(pct, [&](int x, int y) {  // spread the particles evenly over the pixels
    while (next < NP && (int)((int64_t)next * m / NP) == k) {
      s.tx[next] = (int16_t)(x * 16 + 8 + (int)(rnd() % 9) - 4), s.ty[next] = (int16_t)(y * 16 + 8 + (int)(rnd() % 9) - 4);
      next++;
    }
    k++;
  });
  for (int i = NP - 1; i > 0; i--) {  // shuffled, so they travel when the number changes
    int j = (int)(rnd() % (i + 1));
    int16_t a = s.tx[i], b = s.ty[i];
    s.tx[i] = s.tx[j], s.ty[i] = s.ty[j], s.tx[j] = a, s.ty[j] = b;
  }
  s.shown = pct;
}

static void swarm_init() {
  Swarm &s = T->s;
  for (int x = 0; x < W; x++) {
    float d = x + 0.5f - Q;
    s.ex[x] = (uint8_t)(d * d / 64);
  }
  for (int i = 0; i < 512; i++) {  // dark green-black, a little lighter in the middle
    float r = sqrtf(i * 64.0f);
    RGB c = mixc(hexc(0x081210), hexc(0x020304), smooth(0, 115, r));
    s.bg[0][i] = rgbd(c, 0), s.bg[1][i] = rgbd(c, 3);
  }
  for (int i = 0; i < 256; i++) s.hot[i] = (uint8_t)(255 * smooth(15, 120, i * 4));  // speed in 4 px/s steps
  for (int i = 0; i < NP; i++) s.ph[i] = (uint8_t)rnd(), s.vx[i] = s.vy[i] = 0;
  s.shown = -1, s.burst_at = -1, s.inflow = 0, s.dtq = 100;
}

static void swarm_frame(uint32_t t, uint32_t dt, float pct, float e, int32_t full) {
  Swarm &s = T->s;
  int want = (int)(pct + 0.5f);
  if (s.shown < 0) {  // first frame: they fly in from all round the rim
    swarm_targets(want);
    for (int i = 0; i < NP; i++) {
      int a = (int)(rnd() & 1023), r = 100 + (int)(rnd() % 18);
      s.x[i] = (Q << 8) + r * T->sinq[a] / 64, s.y[i] = (Q << 8) - r * T->sinq[(a + 256) & 1023] / 64;
    }
  } else if (want != s.shown) swarm_targets(want);
  RGB cc = charge_colour(pct);
  s.cr = (uint8_t)cc.r, s.cg = (uint8_t)cc.g, s.cb = (uint8_t)cc.b;
  if (full >= 1000 && s.burst_at < 0) s.burst_at = full;  // it was full before the screen appeared: no burst
  if (full >= 0 && s.burst_at < 0) {  // 100 %: everything bursts outwards, then re-forms
    for (int i = 0; i < NP; i++) {
      float dx = s.x[i] / 256.0f - Q, dy = s.y[i] / 256.0f - Q, d = sqrtf(dx * dx + dy * dy) + 1;
      float v = (140 + 160 * frand()) * 16;
      s.vx[i] = (int16_t)clampi(s.vx[i] + (int)(dx / d * v), -32000, 32000), s.vy[i] = (int16_t)clampi(s.vy[i] + (int)(dy / d * v), -32000, 32000);
    }
    s.burst_at = full;
  }
  if (full < 0) s.burst_at = -1;
  // each particle circles its place: radius rho, faster and wider with more power
  int rho = (int)((full >= 0 ? 0.4f : 0.5f + 2.6f * e) * 16);  // Q4 px
  float om = full >= 0 ? 1.5f : 2 + 7 * e;                      // radians a second
  int a1 = (int)(om * t / 1000.0f / (2 * PI) * 1024) & 1023, a2 = (int)(om * 1.3f * t / 1000.0f / (2 * PI) * 1024) & 1023;
  // some leave for the rim and stream back in
  s.inflow += (full >= 0 ? 0 : 3 + 40 * e) * dt / 1000.0f;
  while (s.inflow >= 1) {
    s.inflow -= 1;
    int i = (int)(rnd() % NP), a = (int)(rnd() & 1023);
    s.x[i] = (Q << 8) + 118 * T->sinq[a] / 64, s.y[i] = (Q << 8) - 118 * T->sinq[(a + 256) & 1023] / 64;
    s.vx[i] = s.vy[i] = 0;
  }
  int stiff = 26, damp10 = 75;  // spring per second squared; damping per second, x 10
  if (full >= 0 && full < 900) stiff = 3, damp10 = 12;
  int steps = dt > 60 ? 2 : 1, dtq = (int)((dt / steps) * 1024 / 1000);
  s.dtq = (int)(dt * 1024 / 1000);
  for (int st = 0; st < steps; st++)
    for (int i = 0; i < NP; i++) {
      int p = s.ph[i] << 2;
      int tx = s.tx[i] + (rho * T->sinq[(a1 + p + 256) & 1023] >> 14), ty = s.ty[i] + (rho * T->sinq[(a2 + p) & 1023] >> 14);
      int dx = (tx * 16 - s.x[i]) >> 4, dy = (ty * 16 - s.y[i]) >> 4;  // Q4 px
      int ax = dx * stiff - s.vx[i] * damp10 / 10, ay = dy * stiff - s.vy[i] * damp10 / 10;
      int vx = clampi(s.vx[i] + (ax * dtq >> 10), -32000, 32000), vy = clampi(s.vy[i] + (ay * dtq >> 10), -32000, 32000);
      s.vx[i] = (int16_t)vx, s.vy[i] = (int16_t)vy;
      s.x[i] += vx * dtq >> 6, s.y[i] += vy * dtq >> 6;
      int rx = (s.x[i] >> 8) - Q, ry = (s.y[i] >> 8) - Q;
      if (rx * rx + ry * ry > 117 * 117) {  // the glass: pulled back to the rim, bouncing a little
        float k = 117.0f / sqrtf((float)(rx * rx + ry * ry));
        s.x[i] = (Q << 8) + (int)(rx * k * 256), s.y[i] = (Q << 8) + (int)(ry * k * 256);
        s.vx[i] = (int16_t)(-vx * 3 / 10), s.vy[i] = (int16_t)(-vy * 3 / 10);
      }
    }
}

// A point at (x4, y4) in Q4 spread over 2 x 2 pixels, inside the clip area only.
static inline void splat(uint16_t *buf, int bw, const lv_area_t *ba, const lv_area_t *clip, int x4, int y4, int r, int g, int b, int w) {
  x4 -= 8, y4 -= 8;
  int x = x4 >> 4, y = y4 >> 4, fx = x4 & 15, fy = y4 & 15;
  if (y < clip->y1 - 1 || y > clip->y2 || x < clip->x1 - 1 || x > clip->x2) return;
  int ws[4] = {(16 - fx) * (16 - fy) * w >> 8, fx * (16 - fy) * w >> 8, (16 - fx) * fy * w >> 8, fx * fy * w >> 8};
  for (int k = 0; k < 4; k++) {
    int px = x + (k & 1), py = y + (k >> 1);
    if (px < clip->x1 || px > clip->x2 || py < clip->y1 || py > clip->y2) continue;
    add_px(buf + (py - ba->y1) * bw + (px - ba->x1), r, g, b, ws[k]);
  }
}

static void swarm_paint(uint16_t *buf, int bw, const lv_area_t *ba, const lv_area_t *clip) {
  const Swarm &s = T->s;
  for (int y = clip->y1; y <= clip->y2; y++) {
    int x0 = T->span[y], x1 = W - 1 - x0;
    if (x0 < clip->x1) x0 = clip->x1;
    if (x1 > clip->x2) x1 = clip->x2;
    uint16_t *row = buf + (y - ba->y1) * bw - ba->x1;
    int ey = s.ex[y];
    for (int x = x0; x <= x1; x++) row[x] = s.bg[(x ^ y) & 1][s.ex[x] + ey];
  }
  int ylo = (clip->y1 - 2) * 16, yhi = (clip->y2 + 2) * 16;  // * 16: a negative number may not be shifted left
  for (int i = 0; i < NP; i++) {
    int x4 = s.x[i] >> 4, y4 = s.y[i] >> 4;
    int ax = s.vx[i] < 0 ? -s.vx[i] : s.vx[i], ay = s.vy[i] < 0 ? -s.vy[i] : s.vy[i];
    int speed = ((ax > ay ? ax + (ay >> 1) : ay + (ax >> 1)) >> 4) >> 2;  // px/s in steps of 4, roughly
    int h = s.hot[speed > 255 ? 255 : speed];
    int px4 = x4 - (s.vx[i] * s.dtq >> 10), py4 = y4 - (s.vy[i] * s.dtq >> 10);  // a frame ago
    if ((y4 < ylo || y4 > yhi) && (py4 < ylo || py4 > yhi)) continue;
    int r = s.cr + ((255 - s.cr) * h >> 8), g = s.cg + ((255 - s.cg) * h >> 8), b = s.cb + ((255 - s.cb) * h >> 8);
    int w = (200 + (55 * h >> 8)) * 2;  // brighter than one pixel can take: the 2 x 2 spread then still shows
    splat(buf, bw, ba, clip, x4, y4, r, g, b, w);
    if (h > 40) {  // fast ones leave a short streak
      splat(buf, bw, ba, clip, (x4 + px4) >> 1, (y4 + py4) >> 1, r, g, b, w * h >> 9);
      splat(buf, bw, ba, clip, px4, py4, r, g, b, w * h >> 10);
    }
  }
}

// ---------------------------------------------------------------- shared
bool begin(Style s) {
  if (T && cur == s) return true;
  end();
  cur = s;
  if (s != Style::Warp && s != Style::Swarm) return false;
  T = (Tables *)malloc(sizeof(Tables));
  if (!T) return false;
  for (int i = 0; i < 1024; i++) T->sinq[i] = (int16_t)lroundf(16384 * sinf(i * 2 * PI / 1024));
  for (int y = 0; y < W; y++) {
    float dy = y + 0.5f - Q;
    int x = 0;
    while (x < Q && (x + 0.5f - Q) * (x + 0.5f - Q) + dy * dy > Q * Q) x++;
    T->span[y] = (uint8_t)x;
  }
  if (s == Style::Warp) warp_init();
  else swarm_init();
  return true;
}

void end() { free(T), T = nullptr; }

void frame(uint32_t t, uint32_t dt, float pct, float watts, int32_t full) {
  if (!T) return;
  float e = watts < 0 ? 0.3f : clampf(watts / 10, 0, 1);  // 10 W or more is flat out; unknown: calm
  if (dt > 200) dt = 200;
  if (cur == Style::Warp) warp_frame(t, dt, pct, e, full);
  else swarm_frame(t, dt, pct, e, full);
}

void paint(lv_draw_ctx_t *dc) {
  if (!T) return;
  const lv_area_t *clip = dc->clip_area, *ba = dc->buf_area;
  if (clip->y1 == 0) frame_count++;
  uint16_t *buf = (uint16_t *)dc->buf;
  int bw = lv_area_get_width(ba);
  if (cur == Style::Warp) warp_paint(buf, bw, ba, clip);
  else swarm_paint(buf, bw, ba, clip);
}

}  // namespace chfx
