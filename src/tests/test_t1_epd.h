#pragma once
// T1 — EPD 3-color display test

#include "test_runner.h"

// ─── Helper: center-print a string at given baseline y using raw EPD ──────────
static void _t1_printCentered(EpdDisplay& epd, const char* str, int16_t y) {
    int16_t  x1, y1;
    uint16_t w, h;
    epd.getTextBounds(str, 0, y, &x1, &y1, &w, &h);
    epd.setCursor((400 - (int16_t)w) / 2 - x1, y);
    epd.print(str);
}

// ─── Helper: solid color fill with contrasting prompt overlay ─────────────────
// bgColor   : fill color (GxEPD_WHITE / GxEPD_BLACK / GxEPD_RED)
// textColor : contrasting text color
// roundLabel: e.g. "Round 1/4 : Full WHITE fill"
// prompt    : e.g. "AP = OK     BOOT = FAIL"
static void _t1_colorRound(Display& disp,
                            uint16_t bgColor, uint16_t textColor,
                            const char* roundLabel, const char* prompt) {
    auto& epd = disp.raw();
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(bgColor);

        epd.setFont(&FreeSansBold9pt7b);
        epd.setTextColor(textColor);
        _t1_printCentered(epd, roundLabel, 36);

        // Horizontal rule near bottom
        epd.drawLine(10, 270, 390, 270, textColor);

        epd.setFont(&FreeSans9pt7b);
        _t1_printCentered(epd, prompt, 289);
    } while (epd.nextPage());
}

// ─── Helper: text demo screen (Round 4) ──────────────────────────────────────
static void _t1_textDemo(Display& disp) {
    auto& epd = disp.raw();
    epd.setFullWindow();
    epd.firstPage();
    do {
        epd.fillScreen(GxEPD_WHITE);

        // ── Title row ────────────────────────────────────────────────────────
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(GxEPD_RED);
        _t1_printCentered(epd, "T1", 36);

        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "EPD Text Rendering Demo", 56);

        epd.drawLine(10, 68, 390, 68, GxEPD_BLACK);

        // ── Font / color samples ──────────────────────────────────────────────
        // Bold BLACK — large
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "Bold Black Large", 110);

        // Normal BLACK — small
        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "Normal black small  0123456789", 140);

        // Bold RED — large
        epd.setFont(&FreeSansBold18pt7b);
        epd.setTextColor(GxEPD_RED);
        _t1_printCentered(epd, "Bold Red Large", 180);

        // Normal RED — small
        epd.setFont(&FreeSans9pt7b);
        epd.setTextColor(GxEPD_RED);
        _t1_printCentered(epd, "Normal red small  !@#$%^&*()", 210);

        // ── Verdict prompt ────────────────────────────────────────────────────
        epd.drawLine(10, 245, 390, 245, GxEPD_BLACK);
        epd.setFont(&FreeSansBold9pt7b);
        epd.setTextColor(GxEPD_BLACK);
        _t1_printCentered(epd, "All text clear, bold/normal & colors correct?", 265);
        epd.setFont(&FreeSans9pt7b);
        _t1_printCentered(epd, "AP = PASS       BOOT = FAIL", 285);
    } while (epd.nextPage());
}

// ─── T1 implementation ────────────────────────────────────────────────────────
inline TestResult runTestT1(Display& disp, TestRunner& runner) {

    Serial.println("[T1] EPD Display Test started");
    Serial.println("[T1] Round 1: WHITE fill  Round 2: BLACK fill  Round 3: RED fill  Round 4: Text demo");

    // ── Round 1: WHITE ────────────────────────────────────────────────────────
    Serial.println("[T1] Round 1/4 - Filling screen WHITE ...");
    _t1_colorRound(disp, GxEPD_WHITE, GxEPD_BLACK,
                   "Round 1/4 : Full WHITE fill",
                   "AP = OK       BOOT = FAIL");
    Serial.println("[T1] WHITE fill shown. Screen should be all white.");
    Serial.println("[T1] >>> waiting for verdict (AP=OK  BOOT=FAIL)");
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] <<< verdict received: FAIL");
        Serial.println("[T1] FAIL - WHITE fill rejected by operator");
        static const char* failMsg[] = { "WHITE fill FAILED", "Press AP button to continue" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 2, "FAIL", "AP=Next test");
        Serial.println("[T1] >>> waiting for AP to continue");
        runner.waitForAP();
        Serial.println("[T1] <<< AP received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] <<< verdict received: OK");
    Serial.println("[T1] Round 1 WHITE: OK");

    // ── Round 2: BLACK ────────────────────────────────────────────────────────
    Serial.println("[T1] Round 2/4 - Filling screen BLACK ...");
    _t1_colorRound(disp, GxEPD_BLACK, GxEPD_WHITE,
                   "Round 2/4 : Full BLACK fill",
                   "AP = OK       BOOT = FAIL");
    Serial.println("[T1] BLACK fill shown. Screen should be all black.");
    Serial.println("[T1] >>> waiting for verdict (AP=OK  BOOT=FAIL)");
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] <<< verdict received: FAIL");
        Serial.println("[T1] FAIL - BLACK fill rejected by operator");
        static const char* failMsg[] = { "BLACK fill FAILED", "Press AP button to continue" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 2, "FAIL", "AP=Next test");
        Serial.println("[T1] >>> waiting for AP to continue");
        runner.waitForAP();
        Serial.println("[T1] <<< AP received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] <<< verdict received: OK");
    Serial.println("[T1] Round 2 BLACK: OK");

    // ── Round 3: RED ─────────────────────────────────────────────────────────
    Serial.println("[T1] Round 3/4 - Filling screen RED ...");
    _t1_colorRound(disp, GxEPD_RED, GxEPD_BLACK,
                   "Round 3/4 : Full RED fill",
                   "AP = OK       BOOT = FAIL");
    Serial.println("[T1] RED fill shown. Screen should be all red.");
    Serial.println("[T1] >>> waiting for verdict (AP=OK  BOOT=FAIL)");
    if (!runner.waitForVerdict()) {
        Serial.println("[T1] <<< verdict received: FAIL");
        Serial.println("[T1] FAIL - RED fill rejected by operator");
        static const char* failMsg[] = { "RED fill FAILED", "Press AP button to continue" };
        disp.showTestScreen(1, "EPD Display Test", failMsg, 2, "FAIL", "AP=Next test");
        Serial.println("[T1] >>> waiting for AP to continue");
        runner.waitForAP();
        Serial.println("[T1] <<< AP received, moving on");
        return TestResult::FAIL;
    }
    Serial.println("[T1] <<< verdict received: OK");
    Serial.println("[T1] Round 3 RED: OK");

    // ── Round 4: Text rendering ───────────────────────────────────────────────
    Serial.println("[T1] Round 4/4 - Text rendering demo screen ...");
    _t1_textDemo(disp);
    Serial.println("[T1] Text demo shown: Bold/Normal x Black/Red.");
    Serial.println("[T1] >>> waiting for verdict (AP=PASS  BOOT=FAIL)");
    bool pass = runner.waitForVerdict();
    Serial.printf("[T1] <<< verdict received: %s\n", pass ? "PASS" : "FAIL");
    return pass ? TestResult::PASS : TestResult::FAIL;
}
