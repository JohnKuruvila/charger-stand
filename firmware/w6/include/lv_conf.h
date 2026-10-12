// LVGL 8.3 settings for the W6 firmware and its desktop preview. Anything not set here keeps LVGL's default
// (lv_conf_internal.h).
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#ifdef ARDUINO
#define LV_COLOR_16_SWAP 1  // the panel takes RGB565 big-endian, so LVGL renders it that way and DMA sends it as is
#else
#define LV_COLOR_16_SWAP 0  // the desktop preview reads pixels in the CPU's own byte order
#endif

#define LV_MEM_SIZE (56U * 1024U)  // 40 KB is 80 % full on the desktop preview with every face and the timer built

#ifdef ARDUINO
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

#define LV_DPI_DEF 188  // 240 px across the 32.4 mm active circle
#define LV_DISP_DEF_REFR_PERIOD 20
// Areas LVGL can track as changed before it gives up and redraws the whole screen (default 32). Rain drops and
// fireworks sparks each mark two small areas a frame; 64 keeps those frames to strips. 0.5 KB.
#define LV_INV_BUF_SIZE 64
// A failed LVGL check (e.g. its memory pool is full) restarts the stand, so the start log records a crash, instead of
// LVGL's default: halting with the screen frozen.
#define LV_ASSERT_HANDLER_INCLUDE <stdlib.h>
#define LV_ASSERT_HANDLER abort();

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_QRCODE 1

#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_UNSCII_16 1  // the Terminal face: a 16 px pixel font, 3 KB
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(w6_text_18)
#define LV_FONT_DEFAULT &w6_text_18

#endif
#endif
