#pragma once
// T7 — SD card R/W test (SPI1/FSPI: CLK=9, MOSI=10, MISO=11, CS=7)
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT7(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T7 not yet implemented.",
        "",
        "SPI1: CLK=9 MOSI=10 MISO=11",
        "CS=7",
        "",
        "Init... --",
        "Write/Read... --",
    };
    disp.showTestScreen(7, "SD Card R/W Test",
                        lines, 7,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
