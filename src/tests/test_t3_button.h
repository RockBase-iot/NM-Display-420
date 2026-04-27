#pragma once
// T3 — Button test (AP key GPIO45, BOOT key GPIO0)

#include "test_runner.h"

static constexpr uint32_t T3_TIMEOUT_MS = 10000;

// Wait for a single button to be pressed within timeout.
// Returns true = pressed, false = timeout.
static bool _t3_waitButton(bool (*isPressed)(), uint32_t timeoutMs) {
    uint32_t start = millis();
    // Wait for press
    while (!isPressed()) {
        if (millis() - start >= timeoutMs) return false;
        delay(10);
    }
    delay(50);  // debounce
    while (isPressed()) { delay(10); }  // wait for release
    return true;
}

inline TestResult runTestT3(Display& disp, TestRunner& runner) {
    Serial.println("[T3] Button Test started");
    Serial.println("[T3] AP=GPIO45  BOOT=GPIO0  timeout=10s each");

    bool apOk   = false;
    bool bootOk = false;

    // ── Phase 1: AP key ───────────────────────────────────────────────────────
    {
        const char* lines[] = {
            "Test each button when prompted.",
            "",
            ">>> Press AP key (GPIO45) now <<<",
            "",
            "BOOT key: [ waiting ]",
        };
        disp.showTestScreen(3, "Button Test", lines, 5, nullptr, nullptr);
        Serial.println("[T3] Waiting for AP key...");

        apOk = _t3_waitButton(TestRunner::apPressed, T3_TIMEOUT_MS);
        Serial.print("[T3] AP key: ");
        Serial.println(apOk ? "OK" : "TIMEOUT");
    }

    // ── Phase 2: BOOT key ─────────────────────────────────────────────────────
    {
        char apStatus[32];
        snprintf(apStatus, sizeof(apStatus), "AP   key (GPIO45): [%s]", apOk ? " OK  " : "FAIL ");
        const char* lines[] = {
            apStatus,
            "",
            ">>> Press BOOT key (GPIO0) now <<<",
        };
        disp.showTestScreen(3, "Button Test", lines, 3, nullptr, nullptr);
        Serial.println("[T3] Waiting for BOOT key...");

        bootOk = _t3_waitButton(TestRunner::bootPressed, T3_TIMEOUT_MS);
        Serial.print("[T3] BOOT key: ");
        Serial.println(bootOk ? "OK" : "TIMEOUT");
    }

    // ── Result ────────────────────────────────────────────────────────────────
    bool pass = apOk && bootOk;

    char apLine[32], bootLine[32];
    snprintf(apLine,   sizeof(apLine),   "AP   key (GPIO45): [%s]", apOk   ? " OK  " : "FAIL ");
    snprintf(bootLine, sizeof(bootLine), "BOOT key (GPIO0):  [%s]", bootOk ? " OK  " : "FAIL ");
    const char* resultLines[] = { apLine, bootLine };

    disp.showTestScreen(3, "Button Test",
                        resultLines, 2,
                        pass ? "PASS" : "FAIL",
                        "AP=Next");

    if (pass) {
        Serial.println("[T3] PASS");
    } else {
        Serial.println("[T3] FAIL - one or more buttons timed out");
    }

    runner.waitForAP();
    return pass ? TestResult::PASS : TestResult::FAIL;
}
