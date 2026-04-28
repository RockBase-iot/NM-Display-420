#include <Arduino.h>
#include "esp_task_wdt.h"
#include "test_runner.h"

// ─── Globals ──────────────────────────────────────────────────────────────────
static TestRunner runner;

void setup() {
    // Disable task watchdog — EPD full refresh can take up to 15 s for 3-color display.
    // arduino-esp32 3.x (IDF 5.x): loopTask is auto-added to task_wdt; the legacy
    // disableCoreXWDT() helpers no longer remove it, leading to spurious
    // "esp_task_wdt_reset(): task not found" errors. Deinit the entire task wdt instead.
    esp_task_wdt_deinit();

    // Run the full factory test sequence (blocking)
    runner.run();
}

void loop() {
    // Never reached — runner.run() ends in an infinite halt loop
    delay(1000);
}
