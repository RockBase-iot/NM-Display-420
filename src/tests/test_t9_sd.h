#pragma once
// T9 — SD card R/W test
//
// Bus:
//   FSPI shared with LoRa (SX12xx). For SD-only access we drive LoRa CS
//   high so it doesn't bus-contend.
//   SCK=9, MOSI=10, MISO=11, SD_CS=7, LoRa_CS=8.
//
// Procedure:
//   1. Pull LoRa NSS high to release the bus.
//   2. SPI.begin(SCK,MISO,MOSI,SD_CS); SD.begin(SD_CS, SPI, 20 MHz).
//   3. Print card type + capacity (GB).
//   4. Sequential write of 512 KiB to /T9_TEST.BIN, measure MB/s.
//   5. Sequential read back, verify content, measure MB/s.
//   6. Delete the test file.
//
// On-screen parameters: Type, Capacity (GB), Write MB/s, Read MB/s, Verify.
// Auto-PASS if init OK, write OK, read OK, verify OK.

#include "test_runner.h"
#include "spi_buses.h"
#include <SPI.h>
#include <SD.h>

#define T9_LOG(fmt, ...) Serial.printf("[T9] " fmt "\n", ##__VA_ARGS__)

#define T9_TEST_PATH       "/T9_TEST.BIN"
#define T9_TEST_BYTES      (512u * 1024u)   // 512 KiB
#define T9_BLOCK_BYTES     4096u            // 4 KiB chunks
#define T9_SPI_HZ          4000000u         // 4 MHz (conservative, SD spec requires
                                            // <=400kHz init then up to 25MHz; 4MHz
                                            // works reliably across all cards we tried)

static const char* _t9_typeStr(uint8_t t) {
    switch (t) {
        case CARD_NONE: return "NONE";
        case CARD_MMC:  return "MMC";
        case CARD_SD:   return "SDSC";
        case CARD_SDHC: return "SDHC";
        default:        return "UNKNOWN";
    }
}

inline TestResult runTestT9(Display& disp, TestRunner& runner) {
    T9_LOG("SD test started");
    T9_LOG("FSPI: SCK=%d MISO=%d MOSI=%d  SD_CS=%d  LoRa_NSS=%d",
           PIN_SD_CLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS, PIN_LORA_NSS);

    // ── 1. Park LoRa CS high so it ignores the bus ───────────────────────────
    pinMode(PIN_LORA_NSS, OUTPUT);
    digitalWrite(PIN_LORA_NSS, HIGH);

    // ── 2. SD init on the dedicated peripheral SPI bus (HSPI) ────────────────
    // spiPeripheral is initialised once in TestRunner::runT0(). The EPD bus
    // (default SPI / FSPI) is untouched, so screen drawing keeps working
    // before, during and after this test.
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    // Pump idle clocks so a freshly powered card finishes its internal init
    // before we send CMD0. SD spec wants >=74 clocks with CS/MOSI high.
    spiPeripheral.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 16; i++) spiPeripheral.transfer(0xFF);  // 128 clocks
    spiPeripheral.endTransaction();

    bool sdOk = SD.begin(PIN_SD_CS, spiPeripheral, T9_SPI_HZ);
    T9_LOG("SD.begin(cs=%d, %u Hz) => %s",
           PIN_SD_CS, (unsigned)T9_SPI_HZ, sdOk ? "OK" : "FAIL");

    if (!sdOk) {
        const char* lines[] = {
            "FSPI: CLK=9 MOSI=10 MISO=11",
            "SD_CS=7 (LoRa NSS=8 high)",
            "",
            "SD.begin() FAIL.",
            "Check card / wiring.",
        };
        disp.showTestScreen(9, "SD Card R/W Test", lines, 5, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    uint8_t  type = SD.cardType();
    uint64_t bytes = SD.cardSize();
    float    capGB = (float)bytes / (1024.0f * 1024.0f * 1024.0f);
    T9_LOG("Card: type=%s size=%.2f GB (%llu bytes)",
           _t9_typeStr(type), capGB, (unsigned long long)bytes);

    if (type == CARD_NONE) {
        const char* lines[] = {
            "SD.begin OK but type=NONE",
            "Card not detected.",
        };
        disp.showTestScreen(9, "SD Card R/W Test", lines, 2, "FAIL", "AP=Next");
        SD.end();
        runner.waitForAP();
        return TestResult::FAIL;
    }

    // ── 3. Prepare a static buffer with a recognizable pattern ───────────────
    static uint8_t buf[T9_BLOCK_BYTES];
    for (uint32_t i = 0; i < T9_BLOCK_BYTES; i++) {
        buf[i] = (uint8_t)(i ^ (i >> 8));
    }

    // ── 4. Write benchmark ───────────────────────────────────────────────────
    if (SD.exists(T9_TEST_PATH)) SD.remove(T9_TEST_PATH);
    File f = SD.open(T9_TEST_PATH, FILE_WRITE);
    if (!f) {
        const char* lines[] = {
            "SD mounted OK.",
            "open(WRITE) FAIL.",
        };
        disp.showTestScreen(9, "SD Card R/W Test", lines, 2, "FAIL", "AP=Next");
        SD.end();
        runner.waitForAP();
        return TestResult::FAIL;
    }

    uint32_t written = 0;
    uint32_t tw0 = millis();
    while (written < T9_TEST_BYTES) {
        size_t n = f.write(buf, T9_BLOCK_BYTES);
        if (n != T9_BLOCK_BYTES) { T9_LOG("write short: %u", (unsigned)n); break; }
        written += n;
    }
    f.flush();
    uint32_t twDt = millis() - tw0;
    f.close();
    float writeMBs = (twDt > 0)
                   ? ((float)written / (1024.0f * 1024.0f)) / ((float)twDt / 1000.0f)
                   : 0.0f;
    T9_LOG("Wrote %u bytes in %u ms => %.2f MB/s",
           (unsigned)written, (unsigned)twDt, writeMBs);

    bool writeOk = (written == T9_TEST_BYTES);

    // ── 5. Read back + verify ────────────────────────────────────────────────
    static uint8_t rbuf[T9_BLOCK_BYTES];
    bool readOk   = false;
    bool verifyOk = false;
    uint32_t readBytes = 0;
    float readMBs = 0.0f;

    f = SD.open(T9_TEST_PATH, FILE_READ);
    if (!f) {
        T9_LOG("open(READ) FAIL");
    } else {
        verifyOk = true;
        uint32_t tr0 = millis();
        while (readBytes < T9_TEST_BYTES) {
            size_t n = f.read(rbuf, T9_BLOCK_BYTES);
            if (n != T9_BLOCK_BYTES) { T9_LOG("read short: %u", (unsigned)n); break; }
            if (memcmp(rbuf, buf, T9_BLOCK_BYTES) != 0) {
                T9_LOG("verify mismatch at offset %u", (unsigned)readBytes);
                verifyOk = false;
                break;
            }
            readBytes += n;
        }
        uint32_t trDt = millis() - tr0;
        f.close();
        readOk = (readBytes == T9_TEST_BYTES);
        readMBs = (trDt > 0)
                ? ((float)readBytes / (1024.0f * 1024.0f)) / ((float)trDt / 1000.0f)
                : 0.0f;
        T9_LOG("Read  %u bytes in %u ms => %.2f MB/s  verify=%s",
               (unsigned)readBytes, (unsigned)trDt, readMBs,
               verifyOk ? "OK" : "FAIL");
    }

    // ── 6. Cleanup ───────────────────────────────────────────────────────────
    SD.remove(T9_TEST_PATH);
    SD.end();
    // Note: spiPeripheral stays initialised — T10 (LoRa) will reuse it.
    // EPD's default SPI bus is independent and untouched.

    bool autoPass = sdOk && writeOk && readOk && verifyOk;

    char l1[40], l2[40], l3[40], l4[40], l5[40];
    snprintf(l1, sizeof(l1), "Type:     %s",          _t9_typeStr(type));
    snprintf(l2, sizeof(l2), "Capacity: %.2f GB",     capGB);
    snprintf(l3, sizeof(l3), "Write:    %.2f MB/s",   writeMBs);
    snprintf(l4, sizeof(l4), "Read:     %.2f MB/s",   readMBs);
    snprintf(l5, sizeof(l5), "Verify:   %s",
             verifyOk ? "OK" : (readOk ? "MISMATCH" : "READ FAIL"));
    const char* lines[] = { l1, l2, l3, l4, l5 };

    if (autoPass) {
        disp.showTestScreen(9, "SD Card R/W Test", lines, 5, "PASS", "AP=Next");
        T9_LOG("PASS (auto)");
        runner.waitForAP();
        return TestResult::PASS;
    }

    disp.showTestScreen(9, "SD Card R/W Test", lines, 5,
                        nullptr, "AP=PASS  BOOT=FAIL");
    T9_LOG("Manual verdict required");
    bool pass = runner.waitForVerdict();
    T9_LOG("Operator verdict: %s", pass ? "PASS" : "FAIL");
    disp.showTestScreen(9, "SD Card R/W Test", lines, 5,
                        pass ? "PASS" : "FAIL", "AP=Next");
    return pass ? TestResult::PASS : TestResult::FAIL;
}
