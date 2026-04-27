#include "test_runner.h"
#include "config.h"

// ─── Test implementations (header-only stubs) ─────────────────────────────────
#include "tests/test_t1_epd.h"
#include "tests/test_t2_led.h"
#include "tests/test_t3_button.h"
#include "tests/test_t4_aht20.h"
#include "tests/test_t5_adc.h"
#include "tests/test_t6_wifi.h"
#include "tests/test_t7_sd.h"
#include "tests/test_t8_lora.h"
#include "tests/test_t9_codec.h"
#include "tests/test_t10_dmic.h"

// ─── Button helpers ───────────────────────────────────────────────────────────

bool TestRunner::apPressed() {
    return digitalRead(PIN_AP_BTN) == LOW;
}

bool TestRunner::bootPressed() {
    return digitalRead(PIN_BOOT_BTN) == LOW;
}

void TestRunner::waitForAP() {
    // Wait for release first (avoid accidental double-trigger)
    while (apPressed()) { delay(10); }
    // Then wait for press
    while (!apPressed()) { delay(10); }
    delay(50);  // simple debounce
    while (apPressed()) { delay(10); }  // wait for release
}

bool TestRunner::waitForVerdict() {
    // Returns true = AP (PASS), false = BOOT (FAIL)
    while (apPressed() || bootPressed()) { delay(10); }  // drain any current press
    while (true) {
        if (apPressed()) {
            delay(50);
            while (apPressed()) { delay(10); }
            return true;
        }
        if (bootPressed()) {
            delay(50);
            while (bootPressed()) { delay(10); }
            return false;
        }
        delay(10);
    }
}

// ─── T0 — System startup & welcome screen ────────────────────────────────────

void TestRunner::runT0() {
    // 1. Serial
    Serial.begin(115200);
    delay(200);

    // 2. PA_CTRL default LOW (amplifier off)
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);

    // 3. Button pins
    pinMode(PIN_AP_BTN,   INPUT_PULLUP);
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

    // 4. Init EPD and show welcome screen
    _display.init();
    _display.showWelcome();

    // 5. Serial log
    Serial.println();
    Serial.println("========================================");
    Serial.print  ("[FACTORY TEST] Board: "); Serial.println(BOARD_NAME);
    Serial.print  ("[FACTORY TEST] FW: ");    Serial.println(FW_VERSION);
    Serial.println("[FACTORY TEST] T0 - System startup OK");
    Serial.println("[FACTORY TEST] Waiting for AP key to start...");
    Serial.println("========================================");

    // 6. Wait for AP key press
    waitForAP();

    Serial.println("[FACTORY TEST] T0 PASS - Starting test sequence");
}

// ─── T11 — Summary ────────────────────────────────────────────────────────────

void TestRunner::runT11() {
    // Test metadata (name, index into _results[], counts SKIP)
    struct TestMeta { uint8_t num; const char* name; };
    static const TestMeta META[] = {
        { 1,  "EPD Display  " },
        { 2,  "RGB LED      " },
        { 3,  "Buttons      " },
        { 4,  "AHT20 Sensor " },
        { 5,  "Battery ADC  " },
        { 6,  "WiFi Scan    " },
        { 7,  "SD Card      " },
        { 8,  "LoRa SPI Bus " },
        { 9,  "ES8311 CODEC " },
        { 10, "DMIC Mic     " },
    };

    // Build display lines
    char lineBuf[10][36];
    const char* lines[10];

    bool anyFail = false;
    char failList[64] = "";

    for (uint8_t i = 0; i < 10; i++) {
        uint8_t num = META[i].num;
        TestResult r = _results[i];
        const char* tag = (r == TestResult::PASS) ? "PASS" :
                          (r == TestResult::SKIP) ? "SKIP" : "FAIL";
        snprintf(lineBuf[i], sizeof(lineBuf[i]), "T%-2u %-13s [%s]",
                 (unsigned)num, META[i].name, tag);
        lines[i] = lineBuf[i];

        if (r == TestResult::FAIL) {
            anyFail = true;
            char tmp[8];
            snprintf(tmp, sizeof(tmp), "T%u,", (unsigned)num);
            strncat(failList, tmp, sizeof(failList) - strlen(failList) - 1);
        }
    }

    // Remove trailing comma from failList
    size_t fl = strlen(failList);
    if (fl > 0 && failList[fl - 1] == ',') failList[fl - 1] = '\0';

    const char* summary = anyFail ? "FAIL" : "PASS";

    _display.showTestScreen(11, "Factory Test Summary",
                            lines, 10,
                            summary, nullptr);

    // Serial output
    Serial.println("========================================");
    Serial.println("[FACTORY TEST] ===== SUMMARY =====");
    for (uint8_t i = 0; i < 10; i++) {
        Serial.print  ("[FACTORY TEST] ");
        Serial.println(lines[i]);
    }
    Serial.print("[FACTORY TEST] Overall: ");
    if (anyFail) {
        Serial.print("FACTORY_TEST=FAIL:");
        Serial.println(failList);
    } else {
        Serial.println("FACTORY_TEST=OK");
    }
    Serial.println("========================================");

    // Halt — wait for power cycle
    while (true) { delay(1000); }
}

// ─── Serial log helper ───────────────────────────────────────────────────────
static void logTestStart(uint8_t n, const char* name) {
    Serial.println("----------------------------------------");
    Serial.printf("[FACTORY TEST] T%u START - %s\n", (unsigned)n, name);
}

static void logTestEnd(uint8_t n, const char* name, TestResult r) {
    const char* tag = (r == TestResult::PASS) ? "PASS" :
                      (r == TestResult::SKIP) ? "SKIP" : "FAIL";
    Serial.printf("[FACTORY TEST] T%u END   - %s  [%s]\n", (unsigned)n, name, tag);
}

// ─── Individual test dispatch wrappers ───────────────────────────────────────

void TestRunner::runT1()  {
    logTestStart(1,  "EPD Display");
    TestResult r = runTestT1(_display, *this);
    storeResult(1, r);
    logTestEnd(1, "EPD Display", r);
}
void TestRunner::runT2()  {
    logTestStart(2,  "WS2812 RGB LED");
    TestResult r = runTestT2(_display, *this);
    storeResult(2, r);
    logTestEnd(2, "WS2812 RGB LED", r);
}
void TestRunner::runT3()  {
    logTestStart(3,  "Button");
    TestResult r = runTestT3(_display, *this);
    storeResult(3, r);
    logTestEnd(3, "Button", r);
}
void TestRunner::runT4()  {
    logTestStart(4,  "AHT20 Sensor");
    TestResult r = runTestT4(_display, *this);
    storeResult(4, r);
    logTestEnd(4, "AHT20 Sensor", r);
}
void TestRunner::runT5()  {
    logTestStart(5,  "Battery ADC");
    TestResult r = runTestT5(_display, *this);
    storeResult(5, r);
    logTestEnd(5, "Battery ADC", r);
}
void TestRunner::runT6()  {
    logTestStart(6,  "WiFi Scan");
    TestResult r = runTestT6(_display, *this);
    storeResult(6, r);
    logTestEnd(6, "WiFi Scan", r);
}
void TestRunner::runT7()  {
    logTestStart(7,  "SD Card");
    TestResult r = runTestT7(_display, *this);
    storeResult(7, r);
    logTestEnd(7, "SD Card", r);
}
void TestRunner::runT8()  {
    logTestStart(8,  "LoRa SPI Bus");
    TestResult r = runTestT8(_display, *this);
    storeResult(8, r);
    logTestEnd(8, "LoRa SPI Bus", r);
}
void TestRunner::runT9()  {
    logTestStart(9,  "ES8311 CODEC");
    TestResult r = runTestT9(_display, *this);
    storeResult(9, r);
    logTestEnd(9, "ES8311 CODEC", r);
}
void TestRunner::runT10() {
    logTestStart(10, "LMD4737 DMIC");
    TestResult r = runTestT10(_display, *this);
    storeResult(10, r);
    logTestEnd(10, "LMD4737 DMIC", r);
}

// ─── Main entry ───────────────────────────────────────────────────────────────

void TestRunner::run() {
    runT0();   // Init + welcome; advances only after AP key
    _state = TestState::T1_EPD;  // kick off test sequence

    while (_state != TestState::DONE) {
        switch (_state) {
            case TestState::T1_EPD:     runT1();  _state = TestState::T2_LED;     break;
            case TestState::T2_LED:     runT2();  _state = TestState::T3_BUTTON;  break;
            case TestState::T3_BUTTON:  runT3();  _state = TestState::T4_AHT20;   break;
            case TestState::T4_AHT20:   runT4();  _state = TestState::T5_ADC;     break;
            case TestState::T5_ADC:     runT5();  _state = TestState::T6_WIFI;    break;
            case TestState::T6_WIFI:    runT6();  _state = TestState::T7_SD;      break;
            case TestState::T7_SD:      runT7();  _state = TestState::T8_LORA;    break;
            case TestState::T8_LORA:    runT8();  _state = TestState::T9_CODEC;   break;
            case TestState::T9_CODEC:   runT9();  _state = TestState::T10_DMIC;   break;
            case TestState::T10_DMIC:   runT10(); _state = TestState::T11_SUMMARY; break;
            case TestState::T11_SUMMARY: runT11(); _state = TestState::DONE;       break;
            default:                    _state = TestState::DONE;                  break;
        }
    }
}
