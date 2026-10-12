// GC9A01 over ESP-IDF spi_master with queued DMA transfers, so the CPU can draw the next band of rows while the
// previous ones are still going out. The init sequence is the GFX library's gc9a01_init_operations, replayed here.
// Pixel data is sent as stored, so it must be RGB565 big-endian (LVGL renders it that way: LV_COLOR_16_SWAP).
#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <driver/spi_master.h>

namespace panel {

constexpr int QDEPTH = 12, POOL = 16;  // transactions in flight; pool slots (> QDEPTH so a slot is free before reuse)
static spi_device_handle_t dev;
static int dc_pin;
static spi_transaction_t pool[POOL];
static uint32_t queued = 0, done = 0;  // transaction sequence numbers
static uint32_t wait_us = 0;           // time spent blocked on the bus, for the frame-rate log

static void IRAM_ATTR pre_cb(spi_transaction_t *t) { gpio_set_level((gpio_num_t)dc_pin, (int)(intptr_t)t->user); }

static void reap_one() {
  spi_transaction_t *r;
  spi_device_get_trans_result(dev, &r, portMAX_DELAY);
  done++;
}

// Blocks until transaction number seq (as returned by send) has gone out.
static void wait_for(uint32_t seq) {
  if (done >= seq) return;
  uint32_t t0 = micros();
  while (done < seq) reap_one();
  wait_us += micros() - t0;
}

static void wait_all() { wait_for(queued); }

// Queues bytes for the panel and returns the transaction's sequence number. data must stay untouched until then.
static uint32_t send(const void *data, size_t len, bool is_data) {
  if (queued - done >= QDEPTH) reap_one();
  spi_transaction_t &t = pool[queued % POOL];
  memset(&t, 0, sizeof t);
  t.length = len * 8;
  t.user = (void *)(intptr_t)is_data;
  if (len <= 4) {
    t.flags = SPI_TRANS_USE_TXDATA;
    memcpy(t.tx_data, data, len);
  } else {
    t.tx_buffer = data;
  }
  spi_device_queue_trans(dev, &t, portMAX_DELAY);
  return ++queued;
}

static void cmd(uint8_t c) { send(&c, 1, false); }
static void cmd(uint8_t c, std::initializer_list<uint8_t> d) {
  cmd(c);
  uint8_t b[4];
  size_t n = 0;
  for (uint8_t v : d) b[n++] = v;
  send(b, n, true);
}

// MADCTL for 0-3 quarter turns clockwise, all BGR. 0x08 is what the GFX library sets (the panel's own orientation);
// MX|MY is the half turn, and MV|MX and MV|MY are the two quarter turns (MV alone, or with both flips, would mirror).
static const uint8_t MADCTL[4] = {0x08, 0x68, 0xC8, 0xA8};
static void rotate(int quarter_turns) { cmd(0x36, {MADCTL[quarter_turns & 3]}); }

static bool begin(int sclk, int mosi, int cs, int dc, int rst, int hz, size_t max_transfer, int quarter_turns) {
  dc_pin = dc;
  pinMode(dc, OUTPUT);
  pinMode(rst, OUTPUT);
  digitalWrite(rst, HIGH), delay(20), digitalWrite(rst, LOW), delay(20), digitalWrite(rst, HIGH), delay(120);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = mosi, bus.miso_io_num = -1, bus.sclk_io_num = sclk;
  bus.quadwp_io_num = -1, bus.quadhd_io_num = -1;
  bus.max_transfer_sz = max_transfer;
  if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;
  spi_device_interface_config_t devcfg = {};
  devcfg.clock_speed_hz = hz, devcfg.mode = 0, devcfg.spics_io_num = cs, devcfg.queue_size = QDEPTH;
  devcfg.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;  // write-only
  devcfg.pre_cb = pre_cb;
  if (spi_bus_add_device(SPI2_HOST, &devcfg, &dev) != ESP_OK) return false;

  const uint8_t *op = gc9a01_init_operations;
  for (size_t i = 0; i < sizeof(gc9a01_init_operations); i++) {
    switch (op[i]) {
      case BEGIN_WRITE: case END_WRITE: break;
      case WRITE_COMMAND_8: cmd(op[++i]); break;
      case WRITE_C8_D8: cmd(op[i + 1]), send(&op[i + 2], 1, true), i += 2; break;
      case WRITE_C8_D16: cmd(op[i + 1]), send(&op[i + 2], 2, true), i += 3; break;
      case WRITE_BYTES: send(&op[i + 2], op[i + 1], true), i += 1 + op[i + 1]; break;
      case DELAY: wait_all(), delay(op[++i]); break;
      default: Serial.printf("panel: unknown init op %u at %u\n", op[i], (unsigned)i); return false;
    }
  }
  cmd(0x21);                          // INVON: the IPS panel shows true colours inverted
  rotate(quarter_turns);              // MADCTL
  cmd(0x2A, {0, 0, 0, 239});          // CASET
  cmd(0x2B, {0, 0, 0, 239});          // RASET
  wait_all();
  wait_us = 0;
  return true;
}

// Starts a pixel write to the rectangle x1..x2, y1..y2 (inclusive). Send the pixels next, row by row.
static void window(int x1, int y1, int x2, int y2) {
  cmd(0x2A, {(uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2});  // CASET
  cmd(0x2B, {(uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2});  // RASET
  cmd(0x2C);                                                                      // RAMWR
}

}  // namespace panel
