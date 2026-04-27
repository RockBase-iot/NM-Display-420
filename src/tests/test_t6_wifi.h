#pragma once
// T6 — WiFi scan test
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT6(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T6 not yet implemented.",
        "",
        "Scanning...",
        "Found APs: --",
    };
    disp.showTestScreen(6, "WiFi Scan Test",
                        lines, 4,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
