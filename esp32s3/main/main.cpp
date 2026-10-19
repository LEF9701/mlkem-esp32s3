// main.cpp (ESP32-S3) - run the ML-KEM experiments on the microcontroller.
#include <cstdio>

#include "bootloader_random.h"  // switch the hardware entropy source on/off
#include "esp_random.h"         // esp_fill_random(): hardware RNG
#include "esp_timer.h"          // esp_timer_get_time(): microseconds since boot
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "experiments.hpp"

// ML-KEM needs secure random bytes. The ESP32-S3 has a hardware RNG.
// Its output is only truly random while an entropy source is running:
// Wi-Fi/Bluetooth, or the ADC-based source we enable in mlkem_task().
extern "C" int randombytes(std::uint8_t* out, std::size_t len) {
  esp_fill_random(out, len);
  return 0;
}

static std::uint64_t now_us() {
  return static_cast<std::uint64_t>(esp_timer_get_time());
}

// Called between benchmark rounds: give the CPU to other tasks for 1 tick
// so the task watchdog does not complain.
static void yield_one_tick() { vTaskDelay(1); }

// ML-KEM keeps its working buffers on the stack. The default main task
// stack (3.5 KB) is far too small (peak use: about 20 KB for ML-KEM-512,
// 28 KB for 768, 38 KB for 1024), so we run in our own task with 48 KB.
static constexpr std::uint32_t kStackBytes = 48 * 1024;

static void mlkem_task(void*) {
  std::printf("\nML-KEM-%d lab on ESP32-S3, running on CPU %d\n",
              pq::kParameterSet, xPortGetCoreID());

  bootloader_random_enable();  // hardware entropy ON (no Wi-Fi in this demo)

  bool ok = true;
  ok &= lab::exp1_known_answer_test();
  ok &= lab::exp2_key_exchange();
  ok &= lab::exp3_tampering();
  lab::exp4_benchmark(now_us, 20, yield_one_tick);

  bootloader_random_disable();

  // Experiment 5: how much of our 48 KB stack did ML-KEM actually use?
  // (On ESP-IDF the "high water mark" is reported in bytes.)
  UBaseType_t unused = uxTaskGetStackHighWaterMark(nullptr);
  std::printf("\n=== Experiment 5: memory ===\n");
  std::printf("  task stack: %u bytes, never used: %u, peak used: %u\n",
              static_cast<unsigned>(kStackBytes), static_cast<unsigned>(unused),
              static_cast<unsigned>(kStackBytes - unused));

  std::printf("\n%s Press RESET (RST/EN) to run again.\n",
              ok ? "All experiments passed." : "Some experiments FAILED.");
  vTaskDelete(nullptr);  // a FreeRTOS task must never just return
}

// ESP-IDF calls app_main() from C, so it must have C linkage.
extern "C" void app_main(void) {
  xTaskCreatePinnedToCore(mlkem_task, "mlkem", kStackBytes, nullptr, 5,
                          nullptr, 1 /* run on CPU 1 */);
}
