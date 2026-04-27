#pragma once
// T3 — Button test (AP key GPIO45, BOOT key GPIO0)
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT3(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T3 not yet implemented.",
        "",
        "AP   key: GPIO45",
        "BOOT key: GPIO0",
        "",
        "AP   [ WAIT ]",
        "BOOT [ WAIT ]",
    };
    disp.showTestScreen(3, "Button Test",
                        lines, 7,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
