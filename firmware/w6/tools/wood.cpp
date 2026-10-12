// Makes src/wood.cpp, the walnut_grain face's wood: flat-sawn walnut with streaks along the grain, waving, arches
// where the cut crosses the rings, a domed shade, a highlight and brass hour markers, dithered to RGB565.
// The wood never changes, so the stand copies it from flash instead of computing it (~13 % busy and ~100 ms stalls
// while the hands moved, firmware 0.6.4). Run tools/make_wood.sh after changing anything here.
// Only the pixels inside the circle are kept: about 90 KB of flash.
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static const int W = 240, OCT = 120 * 121 / 2;
static uint8_t oct_r[OCT], oct_a[OCT], half_w[240];
static int16_t sinq[1024];
static uint8_t fade[256], shade[170], hx[240], hy[240], grid[31][31][3];
static int16_t arch[240];

static uint8_t u8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }
static uint32_t hash32(uint32_t x) {
  x ^= x >> 16, x *= 0x7FEB352D, x ^= x >> 15, x *= 0x846CA68B, x ^= x >> 16;
  return x;
}
static int vnoise8(int X, int Y) {  // value noise, 0-255, at lattice coordinates in Q8
  int xi = X >> 8, yi = Y >> 8, fx = fade[X & 255], fy = fade[Y & 255];
  auto h = [](int a, int b) { return (int)(hash32((uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u) & 255); };
  int h00 = h(xi, yi), h10 = h(xi + 1, yi), h01 = h(xi, yi + 1), h11 = h(xi + 1, yi + 1);
  int a = h00 + (((h10 - h00) * fx) >> 8), b = h01 + (((h11 - h01) * fx) >> 8);
  return a + (((b - a) * fy) >> 8);
}

// Radius and full angle (2048 a turn, clockwise from 3 o'clock) of pixel (x, y)
struct Polar { int r, ang; };
static Polar polar(int x, int y) {
  int X2 = 2 * x - 239, Y2 = 2 * y - 239, ix = (X2 < 0 ? -X2 : X2) >> 1, iy = (Y2 < 0 ? -Y2 : Y2) >> 1;
  int a = ix > iy ? ix : iy, b = ix > iy ? iy : ix, i = a * (a + 1) / 2 + b;
  int o = oct_a[i], base = iy > ix ? 512 - o : o;
  int ang = X2 > 0 ? (Y2 > 0 ? base : 2048 - base) : (Y2 > 0 ? 1024 - base : 1024 + base);
  return {oct_r[i], ang & 2047};
}

static const uint8_t BAYER4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static uint16_t dithered(int r, int g, int b, int x, int y) {  // RGB565 without visible banding
  int d = BAYER4[y & 3][x & 3];
  r += d >> 1, g += d >> 2, b += d >> 1;
  r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
  return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

static void tables() {
  for (int a = 0; a < 120; a++)
    for (int b = 0; b <= a; b++) {
      float X = 2 * a + 1, Y = 2 * b + 1;  // half pixels from the centre, which lies between pixels 119 and 120
      int i = a * (a + 1) / 2 + b;
      oct_r[i] = (uint8_t)lroundf(sqrtf(X * X + Y * Y) / 2);
      int an = (int)lroundf(atan2f(Y, X) / (3.14159265f / 4) * 256);
      oct_a[i] = an > 255 ? 255 : an;
    }
  for (int y = 0; y < 240; y++) {
    float dy = y + 0.5f - 120;
    half_w[y] = (uint8_t)ceilf(sqrtf(fmaxf(0, 120 * 120 - dy * dy)));
  }
  for (int i = 0; i < 1024; i++) sinq[i] = (int16_t)lroundf(16384 * sinf(i * 2 * 3.14159265f / 1024));
  for (int i = 0; i < 256; i++) {
    float t = i / 256.0f;
    fade[i] = (uint8_t)(t * t * (3 - 2 * t) * 255);
  }
  for (int r = 0; r < 170; r++) {
    float d = r / 120.0f;
    shade[r] = u8(255 * (1.05f - 0.55f * d * d * d) / 1.05f);
  }
  for (int x = 0; x < 240; x++) hx[x] = u8(255 * expf(-(x - 75.0f) * (x - 75.0f) / (2 * 38 * 38)));
  for (int y = 0; y < 240; y++) {
    hy[y] = u8(255 * expf(-(y - 65.0f) * (y - 65.0f) / (2 * 38 * 38)));
    float fy = y - 120;
    arch[y] = (int16_t)(0.0009f * (fy + 40) * (fy + 40) * 256);  // where the cut crosses the rings: cathedral arches
  }
  for (int j = 0; j < 31; j++)  // the slow noise (warp in px x 8, ring wobble, purple) every 8 px, interpolated between
    for (int i = 0; i < 31; i++) {
      int x = i * 8, y = j * 8;
      grid[j][i][0] = (uint8_t)((vnoise8(x * 3, y) * 26 + vnoise8(x * 13, y * 5) * 4) * 8 / 255);
      grid[j][i][1] = (uint8_t)vnoise8(x * 5, y * 3);
      grid[j][i][2] = (uint8_t)vnoise8(x * 3 + 7 * 256, y * 3);
    }
}

static uint16_t wood(int x, int y) {
  int n[3], ix = x >> 3, iy = y >> 3, fx = (x & 7) * 32, fy = (y & 7) * 32;
  for (int k = 0; k < 3; k++) {
    int a = grid[iy][ix][k] + ((grid[iy][ix + 1][k] - grid[iy][ix][k]) * fx >> 8);
    int b2 = grid[iy + 1][ix][k] + ((grid[iy + 1][ix + 1][k] - grid[iy + 1][ix][k]) * fx >> 8);
    n[k] = a + ((b2 - a) * fy >> 8);
  }
  int gx = x * 256 + n[0] * 32 + arch[y];               // Q8 px; n[0] is the warp in px x 8
  int streak = 16384 + sinq[(gx * 68 >> 8) & 1023];     // 0..32768
  int ring = 16384 + sinq[((gx * 14 >> 8) + n[1] * 489 / 255) & 1023];
  int h0 = hash32(x * 2654435761u ^ (y >> 4)) & 255, h1 = hash32(x * 2654435761u ^ ((y >> 4) + 1)) & 255;
  int fine = h0 + ((h1 - h0) * (y & 15) >> 4);  // fibre: random across the grain, smooth along it
  int tq = (ring * 166 >> 15) + (streak * 38 >> 15) + ((fine - 128) * 90 >> 8);  // Q8
  tq = tq < 0 ? 0 : tq > 255 ? 255 : tq;
  int r = 0x7A + ((0x2E - 0x7A) * tq >> 8), g = 0x4C + ((0x1A - 0x4C) * tq >> 8), b = 0x2C + ((0x0F - 0x2C) * tq >> 8);
  int pu = n[2] * 64 >> 8;  // a little purple in places
  r += ((0x4A - r) * pu) >> 8, g += ((0x2A - g) * pu) >> 8, b += ((0x26 - b) * pu) >> 8;
  Polar p = polar(x, y);
  int k = shade[p.r], hl = (hx[x] * hy[y] >> 8) * 54 >> 8;  // dome and highlight
  r = (r * k >> 8) + (255 * hl >> 8), g = (g * k >> 8) + (230 * hl >> 8), b = (b * k >> 8) + (200 * hl >> 8);
  if (p.r >= 96 && p.r < 108) {  // brass markers, longer at 12, 3, 6 and 9
    int pos = ((p.ang + 512) & 2047) * 12, da = pos & 2047;
    da = da < 1024 ? da : 2048 - da;
    int hour = ((pos + 1024) >> 11) % 12;
    if (da < (hour % 3 == 0 ? 225 : 123)) {
      int sh = 180 + (108 - p.r) * 6;
      r = 230 * sh >> 8, g = 185 * sh >> 8, b = 110 * sh >> 8;
    }
  }
  return dithered(r, g, b, x, y);
}

int main() {
  tables();
  printf("// Walnut grain's wood, made by tools/make_wood.sh from tools/wood.cpp. Don't edit: change wood.cpp and run it.\n");
  printf("#include <stdint.h>\n\n");
  printf("// Each row's half-width: row y holds pixels 120 - w to 119 + w\n");
  printf("extern const uint8_t WOOD_HALF_W[240] = {");
  for (int y = 0; y < W; y++) printf("%s%d", y % 24 ? ", " : y ? ",\n    " : "\n    ", half_w[y]);
  printf("};\n\n");
  int n = 0;
  printf("extern const uint16_t WOOD_ROW[240] = {");  // where each row starts in WOOD
  for (int y = 0; y < W; y++) printf("%s%d", y % 16 ? ", " : y ? ",\n    " : "\n    ", n), n += 2 * half_w[y];
  printf("};\n\n");
  printf("// RGB565, in the CPU's byte order\nextern const uint16_t WOOD[%d] = {", n);
  int i = 0;
  for (int y = 0; y < W; y++)
    for (int x = 120 - half_w[y]; x < 120 + half_w[y]; x++, i++)
      printf("%s0x%04X", i % 12 ? ", " : i ? ",\n    " : "\n    ", wood(x, y));
  printf("};\n");
}
