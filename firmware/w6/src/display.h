// The GC9A01 panel under LVGL: two band buffers, so LVGL draws one while DMA sends the other.
#pragma once
#include <Arduino.h>

namespace display {

bool begin(int quarter_turns);
// Turns the picture (0-3 quarter turns clockwise) and redraws the screen.
void set_rotation(int quarter_turns);
// Redraws the whole screen and writes it to out as a 240 x 240, 16-bit BMP, or run-length coded ("W6R1", README) when
// rle is set (GET /api/screenshot). start() is called once the 2 KB to send it with is there, to begin the reply; false
// when it wasn't, and nothing was sent. Once out takes less than it is given (the client has gone), the rest isn't sent.
bool screenshot(Print &out, bool rle, void (*start)());

}  // namespace display
