#pragma once
// T6 — AHT20 temperature & humidity sensor test
//
// Hardware:
//   - AHT20 on shared I2C bus: SDA=GPIO39, SCL=GPIO38
//   - Sensor power enable: GPIO40 HIGH (PIN_TEMP_CTL)
//   - I2C address: 0x38
//
// Test sequence:
//   1. Power on AHT20, init Wire, probe 0x38
//   2. Init Adafruit_AHTX0
//   3. Take 3 samples (1s apart), display min/max/avg
//   4. Auto-PASS if all 3 reads succeed AND values are in plausible range,
//      otherwise show data and ask operator (AP=PASS / BOOT=FAIL).

#include "test_runner.h"
#include <Wire.h>
#include <Adafruit_AHTX0.h>

#define T6_LOG(fmt, ...) Serial.printf("[T6] " fmt "\n", ##__VA_ARGS__)

#define AHT20_I2C_ADDR     0x38
#define T6_MIN_DURATION_MS 8000   // sample for at least this long
#define T6_SAMPLE_GAP_MS   500    // ~16 samples in 8s

// Plausible ranges for indoor / lab environment.
#define T6_TEMP_MIN_C      -10.0f
#define T6_TEMP_MAX_C       60.0f
#define T6_HUMI_MIN_PCT      0.0f
#define T6_HUMI_MAX_PCT    100.0f

inline TestResult runTestT6(Display& disp, TestRunner& runner) {
    T6_LOG("AHT20 test started");
    T6_LOG("I2C SDA=%d SCL=%d  PWR=%d (HIGH)", PIN_I2C_SDA, PIN_I2C_SCL, PIN_TEMP_CTL);

    // ── 0. Intro screen ─────────────────────────────────────────────────────
    // showTestScreen is a *blocking* full EPD refresh (~15 s). When it
    // returns, the panel is already showing this content. The following
    // delay keeps the intro on screen long enough for the operator to read
    // it before the (fast, ~3 s) sample loop starts and the next full
    // refresh begins.
    {
        const char* introLines[] = {
            ">>> T6 starting <<<",
            "",
            "I2C SDA=39  SCL=38  Addr=0x38",
            "Power on sensor (GPIO40)...",
            "",
            "Will sample temperature & humidity",
            "for at least 8 seconds, please wait...",
        };
        disp.showTestScreen(6, "AHT20 - Sampling",
                            introLines, 7,
                            nullptr, "Please wait...");
    }
    delay(1500);  // hold intro visible

    // ── 1. Power on sensor ───────────────────────────────────────────────────
    pinMode(PIN_TEMP_CTL, OUTPUT);
    digitalWrite(PIN_TEMP_CTL, HIGH);
    delay(50);  // AHT20 needs ~40ms after power-on before first command

    // ── 2. I2C bus + probe ───────────────────────────────────────────────────
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.beginTransmission(AHT20_I2C_ADDR);
    bool i2cOk = (Wire.endTransmission() == 0);
    T6_LOG("I2C probe 0x%02X => %s", AHT20_I2C_ADDR, i2cOk ? "ACK" : "NAK");

    // NOTE: We deliberately do NOT render an intermediate "Reading sensor..."
    // screen here — a full EPD refresh takes ~15s, longer than the whole
    // sample loop (3s), and back-to-back full refreshes would overlap and
    // leave the previous frame on screen. We render the result ONCE at the
    // end. Progress is visible on the serial console.

    if (!i2cOk) {
        T6_LOG("FAIL: AHT20 not on bus");
        const char* lines[] = {
            "I2C SDA=39  SCL=38",
            "Addr=0x38: NAK",
            "",
            "AHT20 not responding.",
            "Check power / wiring.",
        };
        disp.showTestScreen(6, "AHT20 Sensor Test", lines, 5, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    // ── 3. Init Adafruit driver ──────────────────────────────────────────────
    Adafruit_AHTX0 aht;
    bool drvOk = aht.begin(&Wire);
    T6_LOG("Adafruit_AHTX0.begin() => %s", drvOk ? "OK" : "FAIL");

    if (!drvOk) {
        const char* lines[] = {
            "I2C probe: ACK",
            "Driver init: FAIL",
            "",
            "AHT20 calib failed.",
        };
        disp.showTestScreen(6, "AHT20 Sensor Test", lines, 4, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    // ── 4. Sample loop ───────────────────────────────────────────────────────
    // Sample continuously for at least T6_MIN_DURATION_MS, aggregating
    // min/max/avg. Holding the loop here also gives the EPD time to finish
    // the running-screen full refresh before we draw the result screen.
    float tMin = 1e9f, tMax = -1e9f, tSum = 0.0f;
    float hMin = 1e9f, hMax = -1e9f, hSum = 0.0f;
    int   ok   = 0;
    int   total = 0;
    uint32_t t_begin = millis();

    while ((millis() - t_begin) < T6_MIN_DURATION_MS) {
        sensors_event_t humEvt, tempEvt;
        bool gotIt = aht.getEvent(&humEvt, &tempEvt);
        total++;
        if (!gotIt) {
            T6_LOG("sample %d: getEvent FAIL", total - 1);
            delay(T6_SAMPLE_GAP_MS);
            continue;
        }
        float t = tempEvt.temperature;
        float h = humEvt.relative_humidity;
        T6_LOG("sample %d: T=%.2fC  H=%.2f%%", total - 1, t, h);

        if (t < tMin) tMin = t;
        if (t > tMax) tMax = t;
        if (h < hMin) hMin = h;
        if (h > hMax) hMax = h;
        tSum += t;
        hSum += h;
        ok++;

        delay(T6_SAMPLE_GAP_MS);
    }

    if (ok == 0) {
        const char* lines[] = {
            "Driver init: OK",
            "Read samples: 0",
            "",
            "Sensor not responding.",
        };
        disp.showTestScreen(6, "AHT20 Sensor Test", lines, 4, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    float tAvg = tSum / ok;
    float hAvg = hSum / ok;
    bool inRange = (tAvg >= T6_TEMP_MIN_C && tAvg <= T6_TEMP_MAX_C &&
                    hAvg >= T6_HUMI_MIN_PCT && hAvg <= T6_HUMI_MAX_PCT);
    bool autoPass = (ok == total) && inRange;

    T6_LOG("Summary: ok=%d/%d  T avg=%.2fC (min=%.2f max=%.2f)  H avg=%.2f%% (min=%.2f max=%.2f)  inRange=%d",
           ok, total, tAvg, tMin, tMax, hAvg, hMin, hMax, (int)inRange);

    // ── 5. Show result, ask operator ─────────────────────────────────────────
    char l1[40], l2[48], l3[48], l4[40], l5[40];
    snprintf(l1, sizeof(l1), "Samples OK: %d / %d", ok, total);
    snprintf(l2, sizeof(l2), "Temp: %.2f C (min %.2f max %.2f)", tAvg, tMin, tMax);
    snprintf(l3, sizeof(l3), "Humi: %.2f %% (min %.2f max %.2f)", hAvg, hMin, hMax);
    snprintf(l4, sizeof(l4), "Range check: %s", inRange ? "OK" : "OUT OF RANGE");
    snprintf(l5, sizeof(l5), "Auto-verdict: %s", autoPass ? "PASS" : "MANUAL");

    const char* resultLines[] = { l1, l2, l3, l4, l5 };

    if (autoPass) {
        disp.showTestScreen(6, "AHT20 Sensor Test", resultLines, 5,
                            "PASS", "AP=Next");
        T6_LOG("PASS (auto)");
        runner.waitForAP();
        return TestResult::PASS;
    }

    disp.showTestScreen(6, "AHT20 Sensor Test", resultLines, 5,
                        nullptr, "AP=PASS  BOOT=FAIL");
    T6_LOG("Manual verdict required");
    bool pass = runner.waitForVerdict();
    T6_LOG("Operator verdict: %s", pass ? "PASS" : "FAIL");
    disp.showTestScreen(6, "AHT20 Sensor Test", resultLines, 5,
                        pass ? "PASS" : "FAIL", "AP=Next");
    return pass ? TestResult::PASS : TestResult::FAIL;
}
