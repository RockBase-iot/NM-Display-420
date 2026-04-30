#pragma once
// T10 鈥?LoRa SPI bus connectivity test
// Shares FSPI with SD (CLK=9, MOSI=10, MISO=11), CS=GPIO8
// RST=GPIO12, BUSY=GPIO13
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT10(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T10 not yet implemented.",
        "",
        "FSPI: CLK=9 MOSI=10 MISO=11",
        "NSS=8  RST=12  BUSY=13",
        "",
        "Reset chip... --",
        "Wait BUSY low... --",
    };
    disp.showTestScreen(10, "LoRa SPI Bus Test",
                        lines, 7,
                        nullptr, "USER=Next");

    runner.waitForUser();
    return TestResult::PASS;
}
