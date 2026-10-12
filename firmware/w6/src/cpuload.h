// How busy the chip is, for GET /api/status ("cpu"). This Arduino core is built without FreeRTOS run-time stats, so
// a hardware timer interrupts about 730 times a second and notes which task was running: the idle task, the main loop
// (screen, web server, Wi-Fi setup) or anything else (Wi-Fi, TCP/IP, timers).
// Its period, 1373 µs, is out of step with the 1 ms FreeRTOS tick, so short work that starts on a tick is still seen.
// A one-second share is an estimate from ~730 samples: good to a few percentage points, closer for the 10 s figures.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace cpuload {

void begin();                           // in setup(); it runs in the main loop's task, which it learns from there
void loop_done(uint32_t work_us);       // at the end of each loop(): how long it worked before delay()
void to_json(JsonObject out);

}  // namespace cpuload
