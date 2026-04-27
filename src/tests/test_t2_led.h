#pragma once
// T2 — WS2812 RGB LED test

#include "test_runner.h"
#include <Adafruit_NeoPixel.h>

// ─── T2 implementation ────────────────────────────────────────────────────────
inline TestResult runTestT2(Display& disp, TestRunner& runner) {
    Serial.println("[T2] WS2812 RGB LED Test started");
    Serial.println("[T2] GPIO47  Count:1");

    // Explicitly force push-pull output on data pin before NeoPixel init.
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

    // ── Init NeoPixel ─────────────────────────────────────────────────────────
    Adafruit_NeoPixel strip(WS2812_COUNT, PIN_LED, NEO_GRB + NEO_KHZ800);
    strip.begin();
    strip.setBrightness(255);  // full brightness for diagnostics
    strip.show();  // all off

    // ── Color sequence ────────────────────────────────────────────────────────
    struct ColorEntry { const char* name; uint32_t color; };
    static const ColorEntry COLORS[] = {
        { "RED",   Adafruit_NeoPixel::Color(255,   0,   0) },
        { "GREEN", Adafruit_NeoPixel::Color(  0, 255,   0) },
        { "BLUE",  Adafruit_NeoPixel::Color(  0,   0, 255) },
        { "WHITE", Adafruit_NeoPixel::Color(255, 255, 255) },
    };
    constexpr uint8_t NUM_COLORS = sizeof(COLORS) / sizeof(COLORS[0]);
    constexpr uint32_t COLOR_DURATION_MS = 3000;

    // ── Show EPD cycling screen ───────────────────────────────────────────────
    static const char* infoLines[] = {
        "LED: GPIO47  Count: 1",
        "Sequence: RED > GREEN > BLUE > WHITE",
        "Each color held for 3 seconds.",
        "",
        "Watch the LED -- all 4 colors should appear.",
    };
    disp.showTestScreen(2, "WS2812 RGB LED Test",
                        infoLines, 5,
                        nullptr, nullptr);

    // ── Cycle colors ──────────────────────────────────────────────────────────
    for (uint8_t i = 0; i < NUM_COLORS; i++) {
        Serial.print("[T2] LED -> ");
        Serial.println(COLORS[i].name);
        strip.fill(COLORS[i].color);
        strip.show();
        delay(COLOR_DURATION_MS);
    }

    // Turn off
    strip.clear();
    strip.show();
    Serial.println("[T2] LED OFF");

    // ── Verdict prompt ────────────────────────────────────────────────────────
    static const char* verdictLines[] = {
        "LED cycled: RED > GREEN > BLUE > WHITE",
        "",
        "Did all 4 colors light up correctly?",
    };
    disp.showTestScreen(2, "WS2812 RGB LED Test",
                        verdictLines, 3,
                        nullptr, "AP=PASS   BOOT=FAIL");

    Serial.println("[T2] AP=PASS  BOOT=FAIL");
    bool pass = runner.waitForVerdict();

    if (pass) {
        Serial.println("[T2] PASS");
        return TestResult::PASS;
    } else {
        Serial.println("[T2] FAIL - operator rejected LED colors");
        static const char* failLines[] = { "LED color check failed.", "Press AP to continue." };
        disp.showTestScreen(2, "WS2812 RGB LED Test",
                            failLines, 2, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }
}
