#include <Arduino.h>
#include "test_runner.h"

// ─── Globals ──────────────────────────────────────────────────────────────────
static TestRunner runner;

void setup() {
    // Disable watchdog — EPD full refresh can take up to 15 s for 3-color display
    disableCore0WDT();
    disableCore1WDT();

    // Run the full factory test sequence (blocking)
    runner.run();
}

void loop() {
    // Never reached — runner.run() ends in an infinite halt loop
    delay(1000);
}
