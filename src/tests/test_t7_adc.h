#pragma once
// T7 — Battery ADC (placeholder — HW not populated on current board)

#include "test_runner.h"
#include "config.h"

inline TestResult runTestT7(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "[SKIP] HW not populated",
        "Skipping...",
    };
    disp.showTestScreen(7, "Battery ADC",
                        lines, 2,
                        "SKIP", "AP=Next");

    runner.waitForAP();
    return TestResult::SKIP;
}
