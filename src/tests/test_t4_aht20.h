#pragma once
// T4 — AHT20 temperature & humidity sensor test
// Stub: full implementation in a future commit.

#include "test_runner.h"

inline TestResult runTestT4(Display& disp, TestRunner& runner) {
    static const char* lines[] = {
        "TODO: T4 not yet implemented.",
        "",
        "I2C SDA=39  SCL=38",
        "Power enable: GPIO40",
        "",
        "Temp: -- C",
        "Humi: -- %",
    };
    disp.showTestScreen(4, "AHT20 Sensor Test",
                        lines, 7,
                        nullptr, "AP=Next");

    runner.waitForAP();
    return TestResult::PASS;
}
