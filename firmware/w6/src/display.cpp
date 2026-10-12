#include "display.h"

#include <lvgl.h>

#include "panel.h"

namespace display {

// GC9A01 pads -> ESP32-C3 SuperMini GPIOs (BUILD.md, "Wiring"); VCC on 3V3, GND on G
constexpr int PIN_SCL = 4, PIN_SDA = 6, PIN_CS = 7, PIN_DC = 5, PIN_RST = 10;
constexpr int SPI_HZ = 40000000;
// each buffer holds 20 rows, 9.4 KB, which leaves room for album art; more rows would save a few SPI transfers per
// frame and nothing visible.
constexpr int W = 240, H = 240, LINES = 20;

static lv_color_t buf1[W * LINES] __attribute__((aligned(4)));
static lv_color_t buf2[W * LINES] __attribute__((aligned(4)));
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t drv;
static Print *capture = nullptr;  // set while a screenshot is being taken
static bool capture_rle = false;
// Screenshots go out in 2 KB blocks: one network write per row makes the raw image take 1.6-2.2 s on
// the stand's own network, with the screen frozen meanwhile. The block is borrowed only while a screenshot runs.
static uint8_t *block = nullptr;
static size_t block_len = 0;
static bool lost = false;  // a write fell short: the client has gone, so the rest isn't sent
constexpr size_t BLOCK = 2048;

static void send_block() {
  if (!lost && capture->write(block, block_len) < block_len) lost = true;
  block_len = 0;
}

static void put(const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  while (n) {
    size_t k = min(n, BLOCK - block_len);
    memcpy(block + block_len, b, k);
    block_len += k, b += k, n -= k;
    if (block_len == BLOCK) send_block();
  }
}

// Run-length coding on 16-bit pixels (the "W6R1" format, README): a byte h, then either one pixel repeated
// (h & 0x7F) + 1 times when h has its top bit set, or h + 1 different pixels. The screens are mostly flat colour, so
// a clock comes to about a tenth of the raw 115 KB.
static void put_rle(const lv_color_t *px, int n) {
  int i = 0;
  while (i < n) {
    int run = 1;
    while (i + run < n && run < 128 && px[i + run].full == px[i].full) run++;
    if (run >= 2) {
      uint8_t h = 0x80 | (run - 1);
      uint16_t v = __builtin_bswap16(px[i].full);
      put(&h, 1), put(&v, 2);
      i += run;
      continue;
    }
    int start = i, len = 0;
    while (i < n && len < 128 && !(i + 1 < n && px[i + 1].full == px[i].full)) i++, len++;
    uint8_t h = len - 1;
    put(&h, 1);
    for (int k = start; k < start + len; k++) {
      uint16_t v = __builtin_bswap16(px[k].full);
      put(&v, 2);
    }
  }
}

static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *px) {
  int w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  panel::wait_all();  // the previous buffer has gone out, so LVGL can draw into it next
  if (capture) {      // a full-screen refresh arrives as full-width bands, top to bottom
    if (capture_rle) put_rle(px, w * h);
    else
      for (int i = 0; i < w * h; i++) {
        uint16_t v = __builtin_bswap16(px[i].full);  // BMP wants little-endian
        put(&v, 2);
      }
  }
  panel::window(a->x1, a->y1, a->x2, a->y2);
  panel::send(px, w * h * 2, true);
  lv_disp_flush_ready(d);
}

bool begin(int quarter_turns) {
  if (!panel::begin(PIN_SCL, PIN_SDA, PIN_CS, PIN_DC, PIN_RST, SPI_HZ, W * LINES * 2, quarter_turns)) return false;
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, W * LINES);
  lv_disp_drv_init(&drv);
  drv.hor_res = W, drv.ver_res = H, drv.flush_cb = flush, drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&drv);
  return true;
}

void set_rotation(int quarter_turns) {
  panel::wait_all();
  panel::rotate(quarter_turns);
  panel::wait_all();
  lv_obj_invalidate(lv_scr_act());
  lv_obj_invalidate(lv_layer_top());
}

bool screenshot(Print &out, bool rle, void (*start)()) {
  block = (uint8_t *)malloc(BLOCK);
  if (!block) return false;
  start();
  block_len = 0, lost = false;
  capture = &out, capture_rle = rle;
  auto u16 = [&](uint16_t v) { put(&v, 2); };
  auto u32 = [&](uint32_t v) { put(&v, 4); };
  if (rle) {
    put("W6R1", 4), u16(W), u16(H);
  } else {
    const uint32_t data = W * H * 2, offset = 14 + 40 + 12;
    u16(0x4D42), u32(offset + data), u32(0), u32(offset);                // file header
    u32(40), u32(W), u32((uint32_t)-H), u16(1), u16(16), u32(3), u32(data), u32(2835), u32(2835), u32(0), u32(0);
    u32(0xF800), u32(0x07E0), u32(0x001F);                               // RGB565 masks
  }
  capture = nullptr;      // nothing captured from what is still pending
  lv_refr_now(NULL);      // finish anything pending, so the capture is one clean full-screen pass
  capture = &out;
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);
  panel::wait_all();
  if (block_len) send_block();
  capture = nullptr;
  free(block), block = nullptr;
  return true;
}

}  // namespace display
