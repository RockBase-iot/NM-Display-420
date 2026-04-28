#include "test_runner.h"
#include "config.h"
#include "spi_buses.h"

// Definition for the second SPI bus (HSPI / SPI3) shared by SD + LoRa.
// EPD keeps using the default `SPI` global (FSPI / SPI2).
SPIClass spiPeripheral(HSPI);

// ─── Test implementations (header-only stubs) ─────────────────────────────────
#include "tests/test_t1_epd.h"
#include "tests/test_t2_led.h"
#include "tests/test_t3_button.h"
#include "tests/test_t4_codec.h"
#include "tests/test_t5_dmic.h"
#include "tests/test_t6_aht20.h"
#include "tests/test_t7_adc.h"
#include "tests/test_t8_wifi.h"
#include "tests/test_t9_sd.h"
#include "tests/test_t10_lora.h"

// ─── Button helpers ───────────────────────────────────────────────────────────
//
// Button reads are gated on the EPD BUSY pin being LOW. This panel
// (GDEY042Z98) drives BUSY HIGH while it is refreshing — and a full-window
// refresh takes ~10–15 s, during which the MCU is blocking inside
// firstPage()/nextPage(). If the operator presses AP/BOOT during that
// window, the press is still active when control returns to the next
// waitForAP()/waitForVerdict(), and the very first poll in those loops
// would consume it as a "new" press, putting the test sequence one step
// ahead of the UI. Treating any sample taken while BUSY=HIGH as "not
// pressed" makes that whole class of stale presses invisible.
//
// Debounce: a candidate press must satisfy BUSY=LOW AND button=LOW at
// every one of N samples taken 10 ms apart (~50 ms total). This rejects
// the millisecond-scale bounce/EMI on the BUSY falling edge and on key
// release that would otherwise sneak past a 2-sample 20 ms window.

static const uint8_t  BTN_DEBOUNCE_SAMPLES = 5;
static const uint8_t  BTN_DEBOUNCE_STEP_MS = 10;

static bool _btnPressedDebounced(uint8_t pin) {
    // Quick reject: BUSY already high, or button already high.
    if (digitalRead(PIN_EPD_BUSY) == HIGH) return false;
    if (digitalRead(pin) != LOW)           return false;

    // Stable-low debounce: BUSY must stay low and button must stay low for
    // BTN_DEBOUNCE_SAMPLES consecutive samples.
    for (uint8_t i = 1; i < BTN_DEBOUNCE_SAMPLES; i++) {
        delay(BTN_DEBOUNCE_STEP_MS);
        if (digitalRead(PIN_EPD_BUSY) == HIGH) return false;
        if (digitalRead(pin) != LOW)           return false;
    }
    return true;
}

bool TestRunner::apPressed()   { return _btnPressedDebounced(PIN_AP_BTN);   }
bool TestRunner::bootPressed() { return _btnPressedDebounced(PIN_BOOT_BTN); }

// Raw (no BUSY-gate, no debounce) — only used for the release gate below
// so we can see whether the operator's finger is still on the key.
static bool _btnHeldRaw(uint8_t pin) { return digitalRead(pin) == LOW; }

// Block until both keys are released. Catches the case where the user
// pressed during a refresh and is still holding when BUSY falls — we want
// the first "press" we report to be a fresh release→press edge.
static void _waitAllReleased() {
    while (_btnHeldRaw(PIN_AP_BTN) || _btnHeldRaw(PIN_BOOT_BTN)) delay(10);
    delay(30);  // mechanical/EMI settle after release
}

void TestRunner::waitForAP() {
    _waitAllReleased();
    while (!apPressed()) { delay(10); }
    delay(50);  // debounce
    while (apPressed()) { delay(10); }  // wait for release
}

bool TestRunner::waitForVerdict() {
    _waitAllReleased();
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

    // 4b. Init the second SPI bus (HSPI) shared by SD card and LoRa.
    // Doing it once here means T9/T10 don't need to call SPI.end()/begin()
    // and the EPD's default SPI bus stays untouched.
    initPeripheralSpi();

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
    // Test metadata. Names mirror README.md sequence table.
    struct TestMeta { uint8_t num; const char* name; };
    static const TestMeta META[] = {
        { 1,  "EPD Display"    },
        { 2,  "WS2812 RGB LED" },
        { 3,  "Buttons"        },
        { 4,  "ES8311 CODEC"   },
        { 5,  "DMIC Mic"       },
        { 6,  "AHT20 Sensor"   },
        { 7,  "Battery ADC"    },
        { 8,  "WiFi Scan"      },
        { 9,  "SD Card R/W"    },
        { 10, "LoRa SPI Bus"   },
    };

    // Build display lines. Block is centered horizontally; rows are
    // left-aligned within the block (tabular look).
    char lineBuf[10][36];
    const char* lines[10];

    bool anyFail = false;
    char failList[64] = "";

    for (uint8_t i = 0; i < 10; i++) {
        uint8_t num = META[i].num;
        TestResult r = _results[i];
        const char* tag = (r == TestResult::PASS) ? "PASS" :
                          (r == TestResult::SKIP) ? "SKIP" : "FAIL";
        // Mono font: "T<num>  <name>........[TAG]" — pad name with dots to
        // a fixed width so [TAG] always lands in the same column.
        // 14 chars is the longest name ("WS2812 RGB LED").
        snprintf(lineBuf[i], sizeof(lineBuf[i]), "T%-2u  %-14s [%s]",
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
                            summary, nullptr,
                            /*linesLeftAlignedBlock=*/true,
                            /*monospaceStartLine=*/0);

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

// Re-sync the EPD before every test so each test starts from a known panel
// state, regardless of what the previous test did to peripherals (WiFi RF,
// SD on HSPI, etc.).
void TestRunner::_preTest() {
    _display.resync();
}

// ─── Individual test dispatch wrappers ───────────────────────────────────────

void TestRunner::runT1()  {
    _preTest();
    logTestStart(1,  "EPD Display");
    TestResult r = runTestT1(_display, *this);
    storeResult(1, r);
    logTestEnd(1, "EPD Display", r);
}
void TestRunner::runT2()  {
    _preTest();
    logTestStart(2,  "WS2812 RGB LED");
    TestResult r = runTestT2(_display, *this);
    storeResult(2, r);
    logTestEnd(2, "WS2812 RGB LED", r);
}
void TestRunner::runT3()  {
    _preTest();
    logTestStart(3,  "Button");
    TestResult r = runTestT3(_display, *this);
    storeResult(3, r);
    logTestEnd(3, "Button", r);
}
void TestRunner::runT4()  {
    _preTest();
    logTestStart(4,  "ES8311 CODEC");
    TestResult r = runTestT4(_display, *this);
    storeResult(4, r);
    logTestEnd(4, "ES8311 CODEC", r);
}
void TestRunner::runT5()  {
    _preTest();
    logTestStart(5,  "LMD4737 DMIC");
    TestResult r = runTestT5(_display, *this);
    storeResult(5, r);
    logTestEnd(5, "LMD4737 DMIC", r);
}
void TestRunner::runT6()  {
    _preTest();
    logTestStart(6,  "AHT20 Sensor");
    TestResult r = runTestT6(_display, *this);
    storeResult(6, r);
    logTestEnd(6, "AHT20 Sensor", r);
}
void TestRunner::runT7()  {
    _preTest();
    logTestStart(7,  "Battery ADC");
    TestResult r = runTestT7(_display, *this);
    storeResult(7, r);
    logTestEnd(7, "Battery ADC", r);
}
void TestRunner::runT8()  {
    _preTest();
    logTestStart(8,  "WiFi Scan");
    TestResult r = runTestT8(_display, *this);
    storeResult(8, r);
    logTestEnd(8, "WiFi Scan", r);
}
void TestRunner::runT9()  {
    _preTest();
    logTestStart(9,  "SD Card");
    TestResult r = runTestT9(_display, *this);
    storeResult(9, r);
    logTestEnd(9, "SD Card", r);
}
void TestRunner::runT10() {
    _preTest();
    logTestStart(10, "LoRa SPI Bus");
    TestResult r = runTestT10(_display, *this);
    storeResult(10, r);
    logTestEnd(10, "LoRa SPI Bus", r);
}

// ─── Main entry ───────────────────────────────────────────────────────────────

void TestRunner::run() {
    runT0();   // Init + welcome; advances only after AP key
    _state = TestState::T1_EPD;  // kick off test sequence

    while (_state != TestState::DONE) {
        switch (_state) {
            case TestState::T1_EPD:     runT1();  _state = TestState::T2_LED;     break;
            case TestState::T2_LED:     runT2();  _state = TestState::T3_BUTTON;  break;
            case TestState::T3_BUTTON:  runT3();  _state = TestState::T4_CODEC;    break;
            case TestState::T4_CODEC:   runT4();  _state = TestState::T5_DMIC;     break;
            case TestState::T5_DMIC:    runT5();  _state = TestState::T6_AHT20;    break;
            case TestState::T6_AHT20:   runT6();  _state = TestState::T7_ADC;      break;
            case TestState::T7_ADC:     runT7();  _state = TestState::T8_WIFI;     break;
            case TestState::T8_WIFI:    runT8();  _state = TestState::T9_SD;       break;
            case TestState::T9_SD:      runT9();  _state = TestState::T10_LORA;    break;
            case TestState::T10_LORA:   runT10(); _state = TestState::T11_SUMMARY; break;
            case TestState::T11_SUMMARY: runT11(); _state = TestState::DONE;       break;
            default:                    _state = TestState::DONE;                  break;
        }
    }
}
