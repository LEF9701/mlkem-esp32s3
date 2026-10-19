// main.cpp (Experiment 6) - does ML-KEM run twice as fast on two cores?
//
// We do the same total amount of work (kTotalExchanges full key exchanges,
// each = keygen + encaps + decaps) in three ways and time it:
//   Phase 1: one task on CPU 1                  -> the baseline
//   Phase 2: two tasks, one on CPU 0, one on CPU 1
//   Phase 3: two tasks, both pinned to CPU 1    -> "fake" parallelism
#include <cstdio>

#include "bootloader_random.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "mlkem.hpp"

extern "C" int randombytes(std::uint8_t* out, std::size_t len) {
  esp_fill_random(out, len);
  return 0;
}

static constexpr std::uint32_t kStackBytes = 48 * 1024;  // per ML-KEM task
static constexpr int kTotalExchanges = 50;  // per phase, split between tasks

// An event group is a set of flag bits that tasks can wait on.
static EventGroupHandle_t g_events;
static constexpr EventBits_t kStartBit = BIT0;  // "go!"
static constexpr EventBits_t kDoneBits[2] = {BIT1, BIT2};

struct Job {
  int rounds;
  EventBits_t done_bit;
  int failures;
  int core_seen;
};

// One complete key exchange, exactly like experiment 4.
static bool one_exchange() {
  pq::KeyPair kp;
  pq::Ciphertext ct;
  pq::SharedSecret a, b;
  if (pq::keygen(kp) != 0) return false;
  if (pq::encaps(ct, a, kp.pk) != 0) return false;
  if (pq::decaps(b, ct, kp.sk) != 0) return false;
  return pq::ct_equal(a, b);
}

static void worker(void* arg) {
  Job* job = static_cast<Job*>(arg);
  job->core_seen = xPortGetCoreID();

  // Wait at the start line until the controller says "go".
  xEventGroupWaitBits(g_events, kStartBit, pdFALSE, pdTRUE, portMAX_DELAY);

  // No vTaskDelay() in here: a whole phase takes about 1 s, well below the
  // 5 s task-watchdog timeout, and a delay would distort the timing.
  for (int i = 0; i < job->rounds; ++i) {
    if (!one_exchange()) job->failures++;
  }

  xEventGroupSetBits(g_events, job->done_bit);
  vTaskDelete(nullptr);
}

// Runs one phase and returns its wall-clock time in microseconds.
static std::int64_t run_phase(const char* title, int n_tasks,
                              const BaseType_t cores[]) {
  xEventGroupClearBits(g_events, kStartBit | kDoneBits[0] | kDoneBits[1]);

  Job jobs[2];
  EventBits_t wait_for = 0;
  for (int i = 0; i < n_tasks; ++i) {
    jobs[i] = Job{kTotalExchanges / n_tasks, kDoneBits[i], 0, -1};
    wait_for |= kDoneBits[i];
    BaseType_t ok = xTaskCreatePinnedToCore(worker, i == 0 ? "mlkemA" : "mlkemB",
                                            kStackBytes, &jobs[i], 5, nullptr,
                                            cores[i]);
    if (ok != pdPASS) {
      std::printf("  could not create task (out of memory?)\n");
      return -1;
    }
  }
  vTaskDelay(pdMS_TO_TICKS(50));  // let the workers reach the start line

  std::int64_t t0 = esp_timer_get_time();
  xEventGroupSetBits(g_events, kStartBit);
  xEventGroupWaitBits(g_events, wait_for, pdFALSE, pdTRUE, portMAX_DELAY);
  std::int64_t t1 = esp_timer_get_time();

  vTaskDelay(pdMS_TO_TICKS(50));  // idle task frees the deleted tasks' stacks

  int failures = 0;
  std::printf("\n%s\n", title);
  for (int i = 0; i < n_tasks; ++i) {
    failures += jobs[i].failures;
    std::printf("  task %c: %d exchanges on CPU %d\n", 'A' + i, jobs[i].rounds,
                jobs[i].core_seen);
  }
  double ms = (t1 - t0) / 1000.0;
  std::printf("  wall time %.1f ms  ->  %.1f exchanges/s  (%d failures)\n", ms,
              kTotalExchanges * 1000.0 / ms, failures);
  return t1 - t0;
}

static void controller(void*) {
  g_events = xEventGroupCreate();
  bootloader_random_enable();  // hardware entropy ON (no Wi-Fi here)

  std::printf("\n=== Experiment 6: ML-KEM-%d on one core vs two cores ===\n",
              pq::kParameterSet);
  std::printf("Each phase does %d full exchanges (keygen+encaps+decaps).\n",
              kTotalExchanges);

  const BaseType_t one[] = {1};
  const BaseType_t both[] = {0, 1};
  const BaseType_t same[] = {1, 1};

  std::int64_t t1 = run_phase("Phase 1: 1 task  (CPU 1)", 1, one);
  std::int64_t t2 = run_phase("Phase 2: 2 tasks (CPU 0 + CPU 1)", 2, both);
  std::int64_t t3 = run_phase("Phase 3: 2 tasks (both on CPU 1)", 2, same);

  bootloader_random_disable();

  if (t1 > 0 && t2 > 0 && t3 > 0) {
    std::printf("\nSpeed-up vs phase 1:  two cores x%.2f,  same core x%.2f\n",
                static_cast<double>(t1) / t2, static_cast<double>(t1) / t3);
  }
  std::printf("Press RESET (RST/EN) to run again.\n");
  vTaskDelete(nullptr);
}

extern "C" void app_main(void) {
  // The controller does no crypto, so a small stack is enough. Priority 4 is
  // below the workers (5), so it never steals CPU time from them.
  xTaskCreatePinnedToCore(controller, "controller", 4096, nullptr, 4, nullptr, 0);
}
