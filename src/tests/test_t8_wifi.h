#pragma once
// T8 — WiFi scan test
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT8(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T8 not yet implemented.",
        "",
        "Scanning...",
        "Found APs: --",
    };
    disp.showTestScreen(8, "WiFi Scan Test",
                        lines, 4,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
