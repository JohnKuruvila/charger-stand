// The living faces (Wormhole, Aurora, Galaxy, Silk, Sky and Walnut grain): the background is computed pixel by pixel,
// like the original display demo, and painted straight into the strip LVGL is redrawing; ui.cpp puts the time and
// the hands on top. The C3 has no FPU, so the per-pixel work is whole numbers and tables; floats are used only once
// a frame or once a minute. The tables exist only while a living face is on (begin() to end()), sized for that face.
// This file is built with -O2 (tools/fast_paint.py): it is where the stand spends most of its time.
#pragma once
#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

namespace lfx {

enum class Kind { Wormhole, Aurora, Galaxy, Silk, Sky, Grain, Count };  // in the order of ui::Face

bool begin(Kind k);  // allocates and builds the face's tables (19-36 KB); false when there isn't the memory
void end();
bool active();
bool moving(Kind k);  // Wormhole to Silk get a new picture every frame; Sky and Grain only change once a minute

// Once a frame, for the moving faces. t: ms since the face appeared (ui.cpp wraps it every 6 h).
void frame(uint32_t t);
// Once a minute: Aurora's colours and Sky's light follow the time of day. minute_of_day: 0-1439
void minute(int minute_of_day);
// Paints the strip being redrawn (call from the art object's LV_EVENT_DRAW_MAIN). Does nothing before begin().
void paint(lv_draw_ctx_t *dc);
uint32_t frames();  // frames drawn since the start, for the frame rate
size_t bytes();     // the size of the face's tables

}  // namespace lfx
