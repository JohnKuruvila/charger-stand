// The animated charging screens (Warp and Swarm): every pixel is computed, like the living faces, and painted into
// the strip LVGL is redrawing; ui.cpp puts the percentage, the watts and the clock on top. The C3 has no FPU, so the
// per-pixel work is whole numbers and tables; floats are used only once a frame. The tables exist only while the
// charging screen is on (begin() to end()). Built with -O2 (tools/fast_paint.py).
#pragma once
#include <lvgl.h>
#include <stdint.h>

namespace chfx {

enum class Style { Classic, Warp, Swarm, Count };  // Classic: the ring, drawn by ui.cpp; nothing here

bool begin(Style s);  // allocates and builds the tables (~22 KB); false when there isn't the memory
void end();
bool active();
Style style();

// Once a frame. t: ms since the screen appeared; pct: the charge being shown; watts < 0 when the phone doesn't say;
// full_ms: ms since the charge reached 100 %, or -1.
void frame(uint32_t t, uint32_t dt, float pct, float watts, int32_t full_ms);
// Paints the strip being redrawn (call from the art object's LV_EVENT_DRAW_MAIN).
void paint(lv_draw_ctx_t *dc);
uint32_t frames();  // strips' frames drawn since the start, for the frame rate

}  // namespace chfx
