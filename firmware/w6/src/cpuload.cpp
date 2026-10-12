#include "cpuload.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace cpuload {

constexpr uint32_t PERIOD_US = 1373;
constexpr uint16_t PER_SECOND = 1000000 / PERIOD_US;  // samples that close a one-second bucket
constexpr int SECONDS = 60;

struct Bucket {
  uint16_t idle, loop, other;  // samples per task
  uint32_t loop_max_us;                 // longest loop() in the second: how long the screen and web server stalled
  uint32_t heap_min;                    // lowest free heap seen at the end of a loop() in the second
};

static Bucket hist[SECONDS];  // finished seconds, a ring; written only by the interrupt
static int head = 0, filled = 0;
static Bucket cur;
static TaskHandle_t idle_task, loop_task;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;  // single core: this masks the interrupt while tasks touch cur

static void IRAM_ATTR fresh(Bucket &b) { b = {}, b.heap_min = UINT32_MAX; }

static void IRAM_ATTR sample() {
  TaskHandle_t t = xTaskGetCurrentTaskHandle();  // the task this interrupt stopped
  if (t == idle_task) cur.idle++;
  else if (t == loop_task) cur.loop++;
  else cur.other++;
  if (cur.idle + cur.loop + cur.other >= PER_SECOND) {
    hist[head] = cur;
    head = (head + 1) % SECONDS;
    if (filled < SECONDS) filled++;
    fresh(cur);
  }
}

void begin() {
  idle_task = xTaskGetIdleTaskHandle();
  loop_task = xTaskGetCurrentTaskHandle();
  fresh(cur);
  hw_timer_t *timer = timerBegin(0, 80, true);  // 80 MHz APB / 80: counts microseconds
  timerAttachInterrupt(timer, sample, true);
  timerAlarmWrite(timer, PERIOD_US, true);
  timerAlarmEnable(timer);
}

void loop_done(uint32_t work_us) {
  uint32_t heap = ESP.getFreeHeap();
  portENTER_CRITICAL(&mux);
  if (work_us > cur.loop_max_us) cur.loop_max_us = work_us;
  if (heap < cur.heap_min) cur.heap_min = heap;
  portEXIT_CRITICAL(&mux);
}

static int pct(uint32_t part, uint32_t total) { return total ? (part * 100 + total / 2) / total : 0; }
static uint32_t total(const Bucket &b) { return b.idle + b.loop + b.other; }
static int busy(const Bucket &b) { return pct(total(b) - b.idle, total(b)); }

void to_json(JsonObject out) {
  // A copy, so the interrupt isn't held off while the JSON is built. Borrowed only for the request: 1 KB is a lot
  // for the web server's stack.
  Bucket *h = (Bucket *)malloc(sizeof(Bucket) * SECONDS);
  if (!h) return;
  portENTER_CRITICAL(&mux);
  int n = filled, start = (head - filled + SECONDS) % SECONDS;
  for (int i = 0; i < n; i++) h[i] = hist[(start + i) % SECONDS];  // oldest first
  portEXIT_CRITICAL(&mux);
  if (!n) return free(h);

  out["busy_pct"] = busy(h[n - 1]);  // the last whole second
  Bucket sum = {};
  uint32_t loop_max = 0;
  for (int i = n < 10 ? 0 : n - 10; i < n; i++) {
    sum.idle += h[i].idle, sum.loop += h[i].loop, sum.other += h[i].other;
    loop_max = max(loop_max, h[i].loop_max_us);
  }
  uint32_t t = total(sum);
  out["busy_pct_10s"] = pct(t - sum.idle, t);
  JsonObject split = out["split_10s"].to<JsonObject>();  // shares of all time, not of the busy part
  split["loop"] = pct(sum.loop, t), split["system"] = pct(sum.other, t);
  out["loop_max_ms_10s"] = roundf(loop_max / 100.0f) / 10;
  int peak = 0;
  for (int i = 0; i < n; i++) peak = max(peak, busy(h[i]));
  out["peak_pct_60s"] = peak;
  // A second each, oldest first. A null heap means no loop() finished in that second.
  JsonArray busy_s = out["history_pct"].to<JsonArray>();
  JsonArray heap_s = out["history_heap_kb"].to<JsonArray>();
  JsonArray stall_s = out["history_loop_max_ms"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    busy_s.add(busy(h[i]));
    if (h[i].heap_min == UINT32_MAX) heap_s.add(nullptr);
    else heap_s.add(h[i].heap_min / 1024);
    stall_s.add(h[i].loop_max_us / 1000);
  }
  free(h);
}

}  // namespace cpuload
