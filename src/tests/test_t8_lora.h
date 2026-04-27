#pragma once
// T8 — LoRa SPI bus connectivity test
// Shares FSPI with SD (CLK=9, MOSI=10, MISO=11), CS=GPIO8
// RST=GPIO12, BUSY=GPIO13
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT8(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T8 not yet implemented.",
        "",
        "FSPI: CLK=9 MOSI=10 MISO=11",
        "NSS=8  RST=12  BUSY=13",
        "",
        "Reset chip... --",
        "Wait BUSY low... --",
    };
    disp.showTestScreen(8, "LoRa SPI Bus Test",
                        lines, 7,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
