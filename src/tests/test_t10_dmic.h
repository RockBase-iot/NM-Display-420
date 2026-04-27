#pragma once
// T10 — LMD4737 DMIC recording test (dual-phase: voice or loopback)
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT10(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T10 not yet implemented.",
        "",
        "Phase-1: Speak at the mic",
        "Phase-2: Loopback (auto)",
        "",
        "Countdown: --s",
        "RMS: ---",
    };
    disp.showTestScreen(10, "LMD4737 DMIC Mic Test",
                        lines, 7,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
