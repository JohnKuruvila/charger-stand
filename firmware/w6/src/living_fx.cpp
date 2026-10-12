// The living faces. See living_fx.h.
#include "living_fx.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t WOOD_HALF_W[240];  // wood.cpp
extern const uint16_t WOOD_ROW[240], WOOD[];

namespace lfx {

constexpr int LV_OCT = 120 * 121 / 2;  // one eighth of the screen: radius and angle of every pixel, mirrored for the rest
constexpr int NSTARS = 1800;
struct WormholeT { uint8_t pal[256][3], fog[170], gu[129], gv[129]; int16_t du[170], dv[170], dh[170]; int tu, tv, th; };
struct AuroraT { uint8_t pal[256][3], kv[720]; int8_t cx[240], cy[240], cxy[480], cr[170]; int16_t ex[240], ey[240]; int tp; };
struct SilkT {
  uint8_t pal[1024][3], kv[720];
  int16_t ex[240], ey[240], px[240], qy[240];
  int16_t bx[240], by[240];  // the parts of px and qy that don't move
};
struct GalaxyT {
  struct { uint16_t ang0, r4; uint8_t spd, cb; } s[NSTARS];  // spd: angle units a second / 8; cb: colour 2 bits, brightness 6
  int16_t x4[NSTARS], y4[NSTARS];                            // where each star is this frame, Q4
  uint8_t g1x[240], g1y[240], g2x[240], g2y[240];            // the core's glow and halo, separable
};
struct SkyT { uint8_t row[240][3], hill1[240], hill2[240], glow[400], disc[400]; int bx, by; bool day; uint8_t hill_c[2][3]; };
struct Living {
  uint8_t oct_r[LV_OCT], oct_a[LV_OCT];  // radius in px (0-169); angle in 1/256ths of 45 degrees
  uint8_t half_w[240];                   // the circle's half-width on each row: pixels outside it aren't painted
  int16_t sinq[1024];                    // sin, Q14, 1024 steps a turn
  union {  // only the face's own part is allocated (size())
    WormholeT wh;
    AuroraT au;
    SilkT si;
    GalaxyT ga;
    SkyT sk;
  };
};
static size_t size(Kind k) {
  static const size_t PART[] = {sizeof(WormholeT), sizeof(AuroraT), sizeof(GalaxyT), sizeof(SilkT), sizeof(SkyT)};
  return k < Kind::Grain ? offsetof(Living, wh) + PART[(int)k] : 0;  // Grain's wood is in flash (wood.cpp)
}
static Living *lvg = nullptr;
static Kind kind = Kind::Count;
static uint32_t frames_drawn = 0;

// Phases for the per-frame tables, in sinq steps (1024 a turn) Q16, kept as whole numbers: the C3 has no FPU, and
// a float sine per table entry cost Aurora and Silk about a sixth of each frame.
constexpr float STEPS = 1024 / 6.2831853f;  // table steps a radian
// rad_per_px radians a pixel, in steps Q16
static constexpr int32_t per_px(float rad_per_px) { return (int32_t)(rad_per_px * STEPS * 65536 + (rad_per_px < 0 ? -0.5f : 0.5f)); }
// rad_per_s radians a second, at t ms, in steps Q16 (wrapping, which the table does too)
static uint32_t at(uint32_t t, float rad_per_s) {
  uint64_t q32 = (uint64_t)(rad_per_s / 1000 * STEPS * 4294967296.0f);  // steps a ms, Q32
  return (uint32_t)((t * q32) >> 16);
}
static inline int sinq16(uint32_t phase) { return lvg->sinq[(phase >> 16) & 1023]; }  // Q14

static float smoothf(float a, float b, float x) {
  float t = (x - a) / (b - a);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
static uint8_t u8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }
static void rgb_mix(uint8_t *o, uint32_t a, uint32_t b, float t) {
  for (int i = 0; i < 3; i++) {
    int s = 16 - 8 * i;
    o[i] = u8(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t);
  }
}

static void living_tables() {
  for (int a = 0; a < 120; a++)
    for (int b = 0; b <= a; b++) {
      float X = 2 * a + 1, Y = 2 * b + 1;  // half pixels from the centre, which lies between pixels 119 and 120
      int i = a * (a + 1) / 2 + b;
      lvg->oct_r[i] = (uint8_t)lroundf(sqrtf(X * X + Y * Y) / 2);
      int an = (int)lroundf(atan2f(Y, X) / (3.14159265f / 4) * 256);
      lvg->oct_a[i] = an > 255 ? 255 : an;
    }
  for (int y = 0; y < 240; y++) {
    float dy = y + 0.5f - 120;
    lvg->half_w[y] = (uint8_t)ceilf(sqrtf(fmaxf(0, 120 * 120 - dy * dy)));
  }
  for (int i = 0; i < 1024; i++) lvg->sinq[i] = (int16_t)lroundf(16384 * sinf(i * 2 * 3.14159265f / 1024));
}

// Radius and full angle (2048 a turn, clockwise from 3 o'clock) of pixel x on a row whose |offset| is iy
struct Polar { int r, ang; };
static inline Polar polar(int x, int y) {
  int X2 = 2 * x - 239, Y2 = 2 * y - 239, ix = (X2 < 0 ? -X2 : X2) >> 1, iy = (Y2 < 0 ? -Y2 : Y2) >> 1;
  int a = ix > iy ? ix : iy, b = ix > iy ? iy : ix, i = a * (a + 1) / 2 + b;
  int o = lvg->oct_a[i], base = iy > ix ? 512 - o : o;
  int ang = X2 > 0 ? (Y2 > 0 ? base : 2048 - base) : (Y2 > 0 ? 1024 - base : 1024 + base);
  return {lvg->oct_r[i], ang & 2047};
}
static inline int radius(int x, int y) {
  int X2 = 2 * x - 239, Y2 = 2 * y - 239, ix = (X2 < 0 ? -X2 : X2) >> 1, iy = (Y2 < 0 ? -Y2 : Y2) >> 1;
  int a = ix > iy ? ix : iy, b = ix > iy ? iy : ix;
  return lvg->oct_r[a * (a + 1) / 2 + b];
}

// RGB565 in the panel's byte order, by shifts: lv_color_make's bitfields cost ~17 cycles a pixel more on the C3
static inline lv_color_t rgb565(int r, int g, int b) {
  uint16_t c = (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
  lv_color_t o;
#if LV_COLOR_16_SWAP
  o.full = (uint16_t)(c >> 8 | c << 8);
#else
  o.full = c;
#endif
  return o;
}

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static inline lv_color_t dithered(int r, int g, int b, int x, int y) {  // RGB565 without visible banding
  int d = BAYER4[y & 3][x & 3];
  r += d >> 1, g += d >> 2, b += d >> 1;
  return rgb565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

static inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7FEB352D, x ^= x >> 15, x *= 0x846CA68B, x ^= x >> 16;
  return x;
}
// ---- building each face's tables
static void wormhole_init() {
  auto &w = lvg->wh;
  static const uint32_t STOPS[5] = {0xFF9A3D, 0xFF3D8B, 0x7B4DFF, 0x2EC8FF, 0xFF9A3D};  // amber, magenta, violet, cyan
  for (int i = 0; i < 256; i++) {
    float k = i / 64.0f;
    int s = (int)k;
    rgb_mix(w.pal[i], STOPS[s], STOPS[s + 1], k - s);
  }
  for (int r = 0; r < 170; r++) {
    w.fog[r] = u8(255 * smoothf(48, 120, r));  // dark around the time
    int dep = r < 8 ? 960 : 30 * 256 / r;     // depth down the tunnel, Q8, and what each ring of it contributes
    w.du[r] = dep * 3, w.dv[r] = dep * 10, w.dh[r] = dep * 410 >> 8;
  }
  for (int d = 0; d <= 128; d++) w.gu[d] = u8(255 * smoothf(18, 0, d)), w.gv[d] = u8(255 * smoothf(23, 0, d));
}

static void aurora_palette(int minute_of_day) {  // colours for the time of day: night, dawn, day, dusk
  static const uint32_t P[4][3] = {{0x0B1A4A, 0x2EC4A0, 0x6B3FD6}, {0x4A1F5C, 0xFF8A5C, 0xFFC27A},
                                   {0x1F6FD1, 0x46D3E6, 0xB8F2FF}, {0x2B1048, 0xE0457B, 0xFF9A3D}};
  static const float START[4] = {0, 6, 12, 18.5f};
  float h = minute_of_day / 60.0f;
  int a = 3;
  for (int i = 0; i < 4; i++)
    if (h >= START[i]) a = i;
  int b = (a + 1) % 4;
  float span = (b ? START[b] : 24) - START[a], k = smoothf(0.6f, 1.0f, (h - START[a]) / span);
  uint8_t c[3][3];
  for (int i = 0; i < 3; i++) rgb_mix(c[i], P[a][i], P[b][i], k);
  for (int i = 0; i < 256; i++) {
    float s = 0.5f + 0.5f * sinf(i * 2 * 3.14159265f / 256), s2 = 0.5f + 0.5f * sinf(i * 4 * 3.14159265f / 256 + 1.3f);
    for (int ch = 0; ch < 3; ch++) {
      float v = c[0][ch] + (c[1][ch] - c[0][ch]) * s;
      lvg->au.pal[i][ch] = u8(v + (c[2][ch] - v) * s2 * 0.7f);
    }
  }
}

// A dimmer centre behind the digits: brightness from lo at the middle to full at the edge, over an ellipse
static void vignette(uint8_t *kv, int16_t *ex, int16_t *ey, float lo, float r0, float r1, int cy) {
  for (int x = 0; x < 240; x++) ex[x] = (int16_t)((x - 120) * (x - 120) * 0.6f / 64);
  for (int y = 0; y < 240; y++) ey[y] = (int16_t)((y - cy) * (y - cy) * 2.2f / 64);
  for (int i = 0; i < 720; i++) kv[i] = u8(255 * (lo + (1 - lo) * smoothf(r0, r1, sqrtf(i * 64.0f))));
}

static void silk_init() {
  auto &s = lvg->si;
  for (int i = 0; i < 1024; i++) {
    float v = sinf(i * 2 * 3.14159265f / 1024), band = 0.5f + 0.5f * v, sheen = powf(1 - fabsf(v), 6);
    uint8_t c[3];
    rgb_mix(c, 0x1A0B2E, 0x7A2E8C, band);
    uint32_t base = (uint32_t)c[0] << 16 | c[1] << 8 | c[2];
    rgb_mix(s.pal[i], base, 0xFFB26B, sheen * 0.85f);
  }
  for (int i = 0; i < 240; i++) {
    float f = (i - 120) / 120.0f;
    s.bx[i] = (int16_t)(f * 3.2f * STEPS), s.by[i] = (int16_t)(f * 1.7f * STEPS);
  }
  vignette(s.kv, s.ex, s.ey, 0.45f, 20, 108, 120);
}

static void galaxy_init() {
  auto &g = lvg->ga;
  uint32_t seed = 0x9E3779B9;
  auto rnd = [&]() { seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5; return (seed >> 8) * (1.0f / 16777216.0f); };
  auto gauss = [&]() { return rnd() + rnd() + rnd() - 1.5f; };
  for (int i = 0; i < NSTARS; i++) {
    float r, th, k = 0.5f + rnd();
    int col;
    if (i < 380) r = fabsf(gauss()) * 22, th = rnd() * 6.2831853f, col = 0;  // the warm bulge
    else {                                                                     // two arms
      r = 12 + powf(rnd(), 0.9f) * 118;
      th = (i & 1) * 3.14159265f + r * 0.045f + gauss() * 0.45f;
      float p = rnd();
      col = p < 0.08f ? 1 : p < 0.15f ? 2 : 3;
    }
    float turns = (0.06f + 0.05f * 30 / (r + 30)) * 0.25f;  // inner stars orbit faster
    g.s[i] = {(uint16_t)(th / 6.2831853f * 65536), (uint16_t)(r * 16), (uint8_t)(turns * 65536 / 8),
              (uint8_t)(col << 6 | (int)(k / 1.5f * 63))};
  }
  for (int x = 0; x < 240; x++) {
    float dx = x + 0.5f - 120;
    g.g1x[x] = u8(255 * expf(-dx * dx / (2 * 11 * 11))), g.g2x[x] = u8(255 * expf(-dx * dx / (2 * 45 * 45)));
  }
  for (int y = 0; y < 240; y++) {
    float dy = (y + 0.5f - 150) / 0.58f;
    g.g1y[y] = u8(255 * expf(-dy * dy / (2 * 11 * 11))), g.g2y[y] = u8(255 * expf(-dy * dy / (2 * 45 * 45)));
  }
}

// ---- once a frame (or a minute): what the frame needs that isn't per pixel
static void wormhole_frame(uint32_t t) {
  auto &w = lvg->wh;
  w.tu = (int)((uint64_t)t * 64 / 1000 & 0xFFFFFF), w.tv = (int)((uint64_t)t * 282 / 1000 & 0xFFFFFF);
  w.th = (int)((uint64_t)t * 20 / 1000 & 255);
}

// Four waves, across, down, diagonally and outwards, each 127 * sin(position * k + time * w)
static void aurora_frame(uint32_t t) {
  auto &a = lvg->au;
  auto wave = [](int8_t *o, int n, float k, uint32_t ph) {
    int32_t step = per_px(k);
    for (int i = 0; i < n; i++) o[i] = (int8_t)(127 * sinq16(ph + i * step) / 16384);
  };
  wave(a.cx, 240, 7 / 240.0f, at(t, 0.35f));
  wave(a.cy, 240, 5 / 240.0f, 0 - at(t, 0.27f));
  wave(a.cxy, 480, 6 / 240.0f, at(t, 0.21f));
  wave(a.cr, 170, 14 / 240.0f, 0 - at(t, 0.5f));
  a.tp = (int)((uint64_t)t * 256 / 100000 & 255);
}

// The folds: px across and qy down, in palette steps, whose sum picks the colour
static void silk_frame(uint32_t t) {
  auto &s = lvg->si;
  constexpr int32_t KX = per_px(2.2f / 120), K1 = per_px(2.6f / 120), K2 = per_px(5.1f / 120);
  uint32_t px0 = 0 - at(t, 0.26f) - 120 * KX, p1 = at(t, 0.30f) - 120 * K1, p2 = 0 - at(t, 0.22f) - 120 * K2;
  constexpr int A = (int)(0.35f * 1.7f * STEPS), A1 = (int)(0.35f * 3.2f * STEPS), A2 = (int)(0.15f * 3.2f * STEPS);
  for (int x = 0; x < 240; x++) s.px[x] = (int16_t)(s.bx[x] + A * sinq16(px0 + x * KX) / 16384);
  int drift = (int)(at(t, 0.18f) >> 16);
  for (int y = 0; y < 240; y++)
    s.qy[y] = (int16_t)((s.by[y] + (A1 * sinq16(p1 + y * K1) + A2 * sinq16(p2 + y * K2)) / 16384 + drift) & 1023);
}

static void galaxy_frame(uint32_t t) {
  auto &g = lvg->ga;
  int cosT = (int)(16384 * cosf(0.95f + 0.10f * sinf(t * 0.0002f)));  // the tilt rocks a little
  uint32_t turn = (uint32_t)((uint64_t)t * 8 * 65536 / 1000);  // 8 x seconds, Q16: x spd is the angle moved, Q16
  for (int i = 0; i < NSTARS; i++) {
    uint16_t a = g.s[i].ang0 + (uint16_t)((g.s[i].spd * turn) >> 16);
    int k = a >> 6;
    int x4 = (g.s[i].r4 * lvg->sinq[(k + 256) & 1023]) >> 14, y4 = (g.s[i].r4 * lvg->sinq[k]) >> 14;
    g.x4[i] = 120 * 16 + x4 - 8, g.y4[i] = 150 * 16 + ((y4 * cosT) >> 14) - 8;
  }
}

static void sky_minute(int minute_of_day) {  // the sky changes slowly: worked out once a minute
  auto &s = lvg->sk;
  float h = minute_of_day / 60.0f, alt = sinf((h - 6) / 12 * 3.14159265f);
  float day = smoothf(-0.05f, 0.35f, alt), tw = 1 - fabsf(alt) / 0.35f;
  tw = tw < 0 ? 0 : tw;
  uint8_t top[3], bot[3], t2[3], b2[3];
  rgb_mix(top, 0x050A1F, 0x2F7FE0, day), rgb_mix(bot, 0x14234A, 0xA8DBFF, day);
  uint32_t T = top[0] << 16 | top[1] << 8 | top[2], B = bot[0] << 16 | bot[1] << 8 | bot[2];
  rgb_mix(t2, T, 0x2B2A6B, tw * 0.7f), rgb_mix(b2, B, 0xFF8E5A, tw * 0.9f);
  uint32_t T2 = t2[0] << 16 | t2[1] << 8 | t2[2], B2 = b2[0] << 16 | b2[1] << 8 | b2[2];
  for (int y = 0; y < 240; y++) rgb_mix(s.row[y], T2, B2, smoothf(0, 1, y / 240.0f));
  s.day = alt > -0.1f;
  float ph = s.day ? (h - 6) / 12 : fmodf(h - 18 + 24, 24) / 12;
  s.bx = (int)(120 - 95 * cosf(ph * 3.14159265f)), s.by = (int)(178 - 120 * sinf(ph * 3.14159265f));
  for (int i = 0; i < 400; i++) {  // by squared distance / 16
    float d = sqrtf(i * 16.0f);
    s.glow[i] = u8(255 * (s.day ? 0.9f * expf(-d / 22) : 0.35f * expf(-d / 18)));
    s.disc[i] = u8(255 * (s.day ? smoothf(10.5f, 9.0f, d) : smoothf(8.5f, 7.0f, d)));
  }
  float lit = (0.35f + 0.65f * day) * 0.6f;
  rgb_mix(s.hill_c[0], 0x2A1A10, 0x6B4A2E, lit), rgb_mix(s.hill_c[1], 0x1A100A, 0x4A3020, lit);
}

static void sky_init() {
  auto &s = lvg->sk;
  for (int x = 0; x < 240; x++) {
    s.hill1[x] = (uint8_t)(186 + 10 * sinf(x * 0.035f + 1.3f) + 5 * sinf(x * 0.09f));
    s.hill2[x] = (uint8_t)(200 + 8 * sinf(x * 0.05f + 4.0f) + 4 * sinf(x * 0.13f + 2));
  }
}

// ---- painting one strip
static inline lv_color_t clamped(int r, int g, int b) {  // moving: no dither needed
  return rgb565(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}

static inline lv_color_t wormhole_px(const WormholeT &w, int x, int y) {
  Polar p = polar(x, y);
  if (p.r < 46) return lv_color_black();
  int u = p.ang * 3 + w.du[p.r] + w.tu, v = w.dv[p.r] + w.tv;
  int fu = u & 255, fv = v & 255, du = fu < 128 ? fu : 256 - fu, dv = fv < 128 ? fv : 256 - fv;
  int grid = LV_MAX(w.gu[du], w.gv[dv]);
  int shade = ((((u >> 8) ^ (v >> 8)) & 1) ? 90 : 179) + (grid * 282 >> 8);
  int k = shade * w.fog[p.r] >> 8;
  const uint8_t *c = w.pal[(w.dh[p.r] - w.th) & 255];
  return clamped(c[0] * k >> 8, c[1] * k >> 8, c[2] * k >> 8);
}

static inline lv_color_t aurora_px(const AuroraT &a, int x, int y) {
  int v = a.cx[x] + a.cy[y] + a.cxy[x + y] + a.cr[radius(x, y)];
  const uint8_t *c = a.pal[((v * 92) >> 8) + a.tp & 255];
  int k = a.kv[LV_MIN(719, a.ex[x] + a.ey[y])];
  return clamped(c[0] * k >> 8, c[1] * k >> 8, c[2] * k >> 8);
}

static inline lv_color_t silk_px(const SilkT &s, int x, int y) {
  const uint8_t *c = s.pal[(s.px[x] + s.qy[y]) & 1023];
  int k = s.kv[LV_MIN(719, s.ex[x] + s.ey[y])];
  return clamped(c[0] * k >> 8, c[1] * k >> 8, c[2] * k >> 8);
}

static inline lv_color_t galaxy_px(const GalaxyT &ga, int x, int y) {
  int v = (ga.g1x[x] * ga.g1y[y] * 230 >> 16) + (ga.g2x[x] * ga.g2y[y] * 31 >> 16);
  return clamped((255 * v >> 8) + 4, (225 * v >> 8) + 4, (180 * v >> 8) + 14);
}

static inline lv_color_t sky_px(const SkyT &s, int x, int y) {
  const uint8_t *c = s.row[y];
  int r = c[0], g = c[1], b = c[2];
  int dx = x - s.bx, dy = y - s.by;
  if (dx > -80 && dx < 80 && dy > -80 && dy < 80) {
    int d2 = (dx * dx + dy * dy) >> 4;
    if (d2 < 400) {
      int a = s.glow[d2] + s.disc[d2];
      if (!s.day) {  // the crescent: the disc minus a slightly offset one
        int ex = dx - 3, ey = dy + 2, e2 = (ex * ex + ey * ey) >> 4;
        a = s.glow[d2] + (s.disc[d2] * (255 - (e2 < 400 ? s.disc[e2] : 0)) >> 8);
      }
      a = a > 255 ? 255 : a;
      uint32_t sc = s.day ? 0xFFF2C2 : 0xDCE6FF;
      r += (((int)(sc >> 16) - r) * a) >> 8, g += (((int)(sc >> 8 & 255) - g) * a) >> 8, b += (((int)(sc & 255) - b) * a) >> 8;
    }
  }
  if (!s.day && y < 170) {
    uint32_t h = hash32(x * 7919 + y * 104729);
    if ((h & 255) < 3) {
      int a = (h >> 16) & 255;
      r += ((255 - r) * a) >> 8, g += ((255 - g) * a) >> 8, b += ((255 - b) * a) >> 8;
    }
  }
  if (y > s.hill2[x]) r = s.hill_c[1][0], g = s.hill_c[1][1], b = s.hill_c[1][2];
  else if (y > s.hill1[x]) r = s.hill_c[0][0], g = s.hill_c[0][1], b = s.hill_c[0][2];
  return dithered(r, g, b, x, y);
}

// One loop for each face, so the face isn't chosen again for every pixel
template <typename T, lv_color_t (*PX)(const T &, int, int)>
static void paint_rows(const T &f, const lv_area_t *clip, const lv_area_t *ba, lv_color_t *buf) {
  int bw = lv_area_get_width(ba);
  for (int y = clip->y1; y <= clip->y2; y++) {
    int hw = lvg->half_w[y], x0 = LV_MAX(clip->x1, 120 - hw), x1 = LV_MIN(clip->x2, 119 + hw);
    lv_color_t *row = buf + (y - ba->y1) * bw - ba->x1;
    for (int x = x0; x <= x1; x++) row[x] = PX(f, x, y);
  }
}

static void paint_stars(const lv_area_t *clip, const lv_area_t *ba, lv_color_t *buf) {  // each over 2 x 2 pixels
  auto &ga = lvg->ga;
  int bw = lv_area_get_width(ba);
  static const uint8_t BASE[4][3] = {{255, 215, 160}, {255, 90, 170}, {255, 230, 140}, {170, 200, 255}};
  for (int i = 0; i < NSTARS; i++) {
    int x4 = ga.x4[i], y4 = ga.y4[i], sx = x4 >> 4, sy = y4 >> 4;
    if (sy < clip->y1 - 1 || sy > clip->y2 || sx < 0 || sx >= 239) continue;
    int fx = x4 & 15, fy = y4 & 15, cb = ga.s[i].cb, br = cb & 63;
    const uint8_t *c = BASE[cb >> 6];
    const int w[4] = {(16 - fx) * (16 - fy), fx * (16 - fy), (16 - fx) * fy, fx * fy};
    for (int k = 0; k < 4; k++) {
      int px = sx + (k & 1), py = sy + (k >> 1);
      if (py < clip->y1 || py > clip->y2 || px < clip->x1 || px > clip->x2) continue;
      lv_color_t *d = buf + (py - ba->y1) * bw + (px - ba->x1);
      uint32_t cur = lv_color_to32(*d);
      int a = w[k] * br * 195 >> 13;  // weight (out of 256) x brightness (up to 1.5): x 3 / 126, without a divide
      int R = ((cur >> 16) & 255) + (c[0] * a >> 8), G = ((cur >> 8) & 255) + (c[1] * a >> 8), B = (cur & 255) + (c[2] * a >> 8);
      *d = clamped(R, G, B);
    }
  }
}

// Walnut grain: the wood never changes, so it was made once (tools/wood.cpp) and is copied from flash
static void paint_wood(const lv_area_t *clip, const lv_area_t *ba, lv_color_t *buf) {
  int bw = lv_area_get_width(ba);
  for (int y = clip->y1; y <= clip->y2; y++) {
    int hw = WOOD_HALF_W[y], x0 = LV_MAX(clip->x1, 120 - hw), x1 = LV_MIN(clip->x2, 119 + hw);
    const uint16_t *src = WOOD + WOOD_ROW[y] - (120 - hw);
    lv_color_t *row = buf + (y - ba->y1) * bw - ba->x1;
    for (int x = x0; x <= x1; x++) {
#if LV_COLOR_16_SWAP
      row[x].full = (uint16_t)(src[x] >> 8 | src[x] << 8);
#else
      row[x].full = src[x];
#endif
    }
  }
}

static void paint_living(lv_draw_ctx_t *dctx, Kind f) {
  const lv_area_t *clip = dctx->clip_area, *ba = dctx->buf_area;
  lv_color_t *buf = (lv_color_t *)dctx->buf;
  if (f == Kind::Grain) {
    if (clip->y1 == 0) frames_drawn++;
    return paint_wood(clip, ba, buf);
  }
  if (!lvg) return;  // no memory for the tables: the time still shows, on black
  if (clip->y1 == 0) frames_drawn++;  // the first strip of a frame
  switch (f) {
    case Kind::Wormhole: return paint_rows<WormholeT, wormhole_px>(lvg->wh, clip, ba, buf);
    case Kind::Aurora: return paint_rows<AuroraT, aurora_px>(lvg->au, clip, ba, buf);
    case Kind::Silk: return paint_rows<SilkT, silk_px>(lvg->si, clip, ba, buf);
    case Kind::Galaxy:
      paint_rows<GalaxyT, galaxy_px>(lvg->ga, clip, ba, buf);
      return paint_stars(clip, ba, buf);
    case Kind::Sky: return paint_rows<SkyT, sky_px>(lvg->sk, clip, ba, buf);
    default: return;
  }
}

// ---- the faces' life
bool moving(Kind k) { return k <= Kind::Silk; }
bool active() { return kind != Kind::Count; }
uint32_t frames() { return frames_drawn; }
size_t bytes() { return lvg ? size(kind) : 0; }

bool begin(Kind k) {
  end();
  if (k == Kind::Grain) return kind = k, true;  // nothing to build: the wood is in flash
  lvg = (Living *)malloc(size(k));
  if (!lvg) return false;
  kind = k;
  living_tables();
  if (k == Kind::Wormhole) wormhole_init();
  if (k == Kind::Silk) silk_init();
  if (k == Kind::Galaxy) galaxy_init();
  if (k == Kind::Aurora) vignette(lvg->au.kv, lvg->au.ex, lvg->au.ey, 0.35f, 30, 112, 112);
  if (k == Kind::Sky) sky_init();
  return true;
}

void end() { free(lvg), lvg = nullptr, kind = Kind::Count; }

void frame(uint32_t t) {
  if (!lvg) return;
  if (kind == Kind::Wormhole) wormhole_frame(t);
  if (kind == Kind::Aurora) aurora_frame(t);
  if (kind == Kind::Silk) silk_frame(t);
  if (kind == Kind::Galaxy) galaxy_frame(t);
}

void minute(int minute_of_day) {
  if (lvg && kind == Kind::Aurora) aurora_palette(minute_of_day);
  if (lvg && kind == Kind::Sky) sky_minute(minute_of_day);
}

void paint(lv_draw_ctx_t *dc) { paint_living(dc, kind); }

}  // namespace lfx
