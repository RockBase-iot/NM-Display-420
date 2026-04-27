#pragma once
// T4 — ES8311 CODEC test (I2C probe + I2S 1kHz tone playback)
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT4(Display& disp, TestRunner& runner) {
    static const char* linesA[] = {
        "TODO: T4 not yet implemented.",
        "",
        "I2C probe (0x18)... --",
    };
    disp.showTestScreen(4, "ES8311 CODEC",
                        linesA, 3,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
