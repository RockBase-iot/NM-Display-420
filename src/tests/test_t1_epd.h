#pragma once
// T1 — EPD 3-color display test

#include "test_runner.h"

// ─── Helper: full-screen solid color fill ─────────────────────────────────────
static void _t1_fillColor(Display& disp, uint16_t color) {
    auto& epd = disp.raw();
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(color);
    } while (epd.nextPage());
}

// ─── T1 implementation ────────────────────────────────────────────────────────
inline TestResult runTestT1(Display& disp, TestRunner& runner) {

    Serial.println("[T1] EPD Display Test started");
    Serial.println("[T1] 4 rounds: WHITE / BLACK / RED / Text render");

    // ── Intro screen ──────────────────────────────────────────────────────────
    {
        static const char* intro[] = {
            "Round 1: Full WHITE fill",
            "Round 2: Full BLACK fill",
            "Round 3: Full RED fill",
            "Round 4: Text rendering",
        };
        disp.showTestScreen(1, "EPD Display Test",
                            intro, 4, nullptr, "AP=Start  BOOT=Skip");
        Serial.println("[T1] Intro screen shown. AP=Start, BOOT=Skip");
    }

    // Allow BOOT to skip the whole T1 test
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] SKIP - user chose to skip EPD test");
        return TestResult::SKIP;
    }

    // ── Round 1: WHITE ────────────────────────────────────────────────────────
    Serial.println("[T1] Round 1: Filling screen WHITE...");
    _t1_fillColor(disp, GxEPD_WHITE);
    Serial.println("[T1] WHITE fill complete. Verify: screen should be all white.");
    Serial.println("[T1] AP=White OK  BOOT=Fail");

    if (!runner.waitForVerdict()) {
        Serial.println("[T1] FAIL - WHITE fill not OK");
        // Restore a readable screen before returning
        static const char* failMsg[] = { "WHITE fill FAILED" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 1, "FAIL", "AP=Next test");
        runner.waitForAP();
        return TestResult::FAIL;
    }
    Serial.println("[T1] Round 1 WHITE: OK");

    // ── Round 2: BLACK ────────────────────────────────────────────────────────
    Serial.println("[T1] Round 2: Filling screen BLACK...");
    _t1_fillColor(disp, GxEPD_BLACK);
    Serial.println("[T1] BLACK fill complete. Verify: screen should be all black.");
    Serial.println("[T1] AP=Black OK  BOOT=Fail");

    if (!runner.waitForVerdict()) {
        Serial.println("[T1] FAIL - BLACK fill not OK");
        static const char* failMsg[] = { "BLACK fill FAILED" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 1, "FAIL", "AP=Next test");
        runner.waitForAP();
        return TestResult::FAIL;
    }
    Serial.println("[T1] Round 2 BLACK: OK");

    // ── Round 3: RED ─────────────────────────────────────────────────────────
    Serial.println("[T1] Round 3: Filling screen RED...");
    _t1_fillColor(disp, GxEPD_RED);
    Serial.println("[T1] RED fill complete. Verify: screen should be all red.");
    Serial.println("[T1] AP=Red OK  BOOT=Fail");

    if (!runner.waitForVerdict()) {
        Serial.println("[T1] FAIL - RED fill not OK");
        static const char* failMsg[] = { "RED fill FAILED" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 1, "FAIL", "AP=Next test");
        runner.waitForAP();
        return TestResult::FAIL;
    }
    Serial.println("[T1] Round 3 RED: OK");

    // ── Round 4: Text rendering ───────────────────────────────────────────────
    Serial.println("[T1] Round 4: Text rendering test...");
    {
        static const char* textLines[] = {
            "BLACK text on WHITE background",
            "RED  text on WHITE background",
            "Bold + Normal fonts rendered",
            "",
            "All text legible and correct?",
        };
        disp.showTestScreen(1, "EPD Display Test",
                            textLines, 5, nullptr, "AP=PASS  BOOT=FAIL");
    }
    Serial.println("[T1] Text render screen shown.");
    Serial.println("[T1] AP=PASS  BOOT=FAIL");

    bool pass = runner.waitForVerdict();
    Serial.printf("[T1] Result: %s\n", pass ? "PASS" : "FAIL");
    return pass ? TestResult::PASS : TestResult::FAIL;
}
