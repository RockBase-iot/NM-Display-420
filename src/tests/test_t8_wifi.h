#pragma once
// T8 — WiFi scan test
//
// Procedure:
//   1. Set WiFi to STA mode (no connect).
//   2. Run a scan, list up to 5 strongest APs (SSID + RSSI + auth).
//   3. Auto-PASS if at least 1 AP is found, otherwise manual verdict.
//
// Notes:
//   - We disconnect & WiFi.mode(WIFI_OFF) at the end to leave the radio idle
//     for the next test.
//   - SSID is truncated to 18 chars to fit on screen.

#include "test_runner.h"
#include <WiFi.h>

#define T8_LOG(fmt, ...) Serial.printf("[T8] " fmt "\n", ##__VA_ARGS__)

#define T8_MAX_DISPLAY_AP   5
#define T8_SCAN_TIMEOUT_MS  15000

static const char* _t8_authStr(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "EAP";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
        default:                        return "?";
    }
}

inline TestResult runTestT8(Display& disp, TestRunner& runner) {
    T8_LOG("WiFi scan test started");

    // ── Intro screen (scan can take ~3-8s, paint *before* we block) ─────────
    {
        const char* introLines[] = {
            "WiFi mode: STA",
            "Scanning 2.4 GHz channels...",
            "",
            "This may take up to 15 seconds.",
        };
        disp.showTestScreen(8, "WiFi Scan Test",
                            introLines, 4,
                            nullptr, "Please wait...");
    }
    // Hold intro on screen for at least T8_MIN_HOLD_MS measured from here so
    // the EPD has fully settled before we draw the result frame; otherwise
    // a fast scan (~3 s) finishes while the controller is still busy from
    // the intro refresh and silently drops the result frame.
    const uint32_t T8_MIN_HOLD_MS = 8000;
    const uint32_t t8_holdStart   = millis();

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);

    T8_LOG("Scanning... (this can take ~3-8s)");
    uint32_t t0 = millis();
    // scanNetworks(async=false, show_hidden=false) — blocks until done
    int n = WiFi.scanNetworks(false, false);
    uint32_t dt = millis() - t0;
    T8_LOG("scanNetworks returned %d in %lums", n, (unsigned long)dt);

    if (n == WIFI_SCAN_FAILED || n < 0) {
        const char* lines[] = {
            "WiFi.scanNetworks() FAILED",
            "Radio init or RF problem.",
        };
        disp.showTestScreen(8, "WiFi Scan Test", lines, 2, "FAIL", "AP=Next");
        WiFi.mode(WIFI_OFF);
        runner.waitForAP();
        return TestResult::FAIL;
    }

    // Collect up to T8_MAX_DISPLAY_AP entries (already sorted by RSSI desc by IDF).
    int show = (n < T8_MAX_DISPLAY_AP) ? n : T8_MAX_DISPLAY_AP;
    T8_LOG("Found %d APs, displaying top %d:", n, show);
    for (int i = 0; i < n; i++) {
        T8_LOG("  [%d] rssi=%d ch=%d auth=%s ssid=\"%s\"",
               i, (int)WiFi.RSSI(i), (int)WiFi.channel(i),
               _t8_authStr(WiFi.encryptionType(i)),
               WiFi.SSID(i).c_str());
    }

    // Build display lines.
    char l0[40];
    snprintf(l0, sizeof(l0), "APs found: %d  (scan %lums)",
             n, (unsigned long)dt);

    char apLines[T8_MAX_DISPLAY_AP][48];
    const char* lines[1 + T8_MAX_DISPLAY_AP];
    int lineCount = 0;
    lines[lineCount++] = l0;

    for (int i = 0; i < show; i++) {
        String s = WiFi.SSID(i);
        if (s.length() > 18) s = s.substring(0, 18);
        if (s.length() == 0) s = "<hidden>";
        snprintf(apLines[i], sizeof(apLines[i]),
                 "%-18s %4d %s",
                 s.c_str(), (int)WiFi.RSSI(i), _t8_authStr(WiFi.encryptionType(i)));
        lines[lineCount++] = apLines[i];
    }

    // Free scan result memory.
    WiFi.scanDelete();

    bool autoPass = (n >= 1);

    // Hold intro on screen until the EPD has fully settled (see comment near
    // t8_holdStart). A fast scan can finish in ~3 s while the panel is still
    // busy refreshing the intro frame.
    {
        uint32_t elapsed = millis() - t8_holdStart;
        if (elapsed < T8_MIN_HOLD_MS) {
            uint32_t wait = T8_MIN_HOLD_MS - elapsed;
            T8_LOG("holding intro for %u ms before result frame", (unsigned)wait);
            delay(wait);
        }
    }

    if (autoPass) {
        disp.showTestScreen(8, "WiFi Scan Test", lines, lineCount,
                            "PASS", "AP=Next");
        T8_LOG("PASS (auto, n=%d)", n);
        WiFi.mode(WIFI_OFF);
        runner.waitForAP();
        return TestResult::PASS;
    }

    // No APs found — could be a shielded room, ask operator.
    disp.showTestScreen(8, "WiFi Scan Test", lines, lineCount,
                        nullptr, "AP=PASS  BOOT=FAIL");
    T8_LOG("No AP found, manual verdict required");
    bool pass = runner.waitForVerdict();
    T8_LOG("Operator verdict: %s", pass ? "PASS" : "FAIL");

    disp.showTestScreen(8, "WiFi Scan Test", lines, lineCount,
                        pass ? "PASS" : "FAIL", "AP=Next");

    WiFi.mode(WIFI_OFF);
    return pass ? TestResult::PASS : TestResult::FAIL;
}
