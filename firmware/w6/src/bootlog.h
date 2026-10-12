// A short history of starts and Wi-Fi drops, so a boot loop, a power cut or a crash can be told apart afterwards
// (GET /api/log). The last 20 starts are kept in flash; Wi-Fi drops are kept in memory since the last start, with a
// count per start in flash. Nothing personal goes in it: times, reasons, uptimes, chip temperature and free memory.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace bootlog {

void begin();                        // first thing in setup()
void opened_setup_by_plugging();     // this start opened setup because the stand was plugged in three times quickly
void loop();                         // keeps this start's record up to date; cheap, call every loop
void restarting(const char *why);    // just before the firmware restarts itself on purpose
void wifi_dropped(uint8_t reason);   // from the Wi-Fi event handler (another task)
const char *reset_text(int reason);  // esp_reset_reason_t as words
void to_json(JsonObject out);

}  // namespace bootlog
