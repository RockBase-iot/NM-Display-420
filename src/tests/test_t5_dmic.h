#pragma once
// T5 - LMD4737 PDM DMIC record + speaker playback test.
//
// Sequence:
//   1. Init I2S RX + ES8311 DMIC capture.
//   2. Record 5s @ 15.625 kHz / 16-bit mono into PSRAM (~156 KB).
//   3. Show RMS / peak.
//   4. Tear down RX, init I2S TX + ES8311 DAC, raise PA_CTRL.
//   5. Play back the recorded buffer (mono -> stereo).
//   6. Lower PA_CTRL, free buffer, wait for AP/BOOT verdict.
//
// Path: LMD4737 (PDM) -> ES8311 DMIC -> PCM -> I2S DIN (GPIO16).
//       Playback: ESP I2S DOUT (GPIO18) -> ES8311 DAC -> NS4150B PA -> speaker.

#include "test_runner.h"
#include "config.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <math.h>

#define T5_DEBUG 1
#if T5_DEBUG
#define T5_LOG(fmt, ...) Serial.printf("[T5] " fmt "\n", ##__VA_ARGS__)
#else
#define T5_LOG(fmt, ...)
#endif

#ifndef ES8311_ADDR
#define ES8311_ADDR 0x18
#endif

// Baseline that's known to deliver actual data from LMD4737 via ES8311 DMIC.
// BCLK = Fs * 16 * 2 = 500 kHz - below LMD4737's spec lower bound (1 MHz)
// but in practice the part still produces a noisy signal that contains audio.
// All other Fs/slot combinations in legacy driver/i2s.h on ESP32-S3 either
// truncate to silence (bits_per_chan=32) or run RX DMA at the wrong rate.
#define T5_SAMPLE_RATE   15625
#define T5_MCLK_HZ       4000000
#define T5_BUF_SAMPLES   512
#define T5_RECORD_SEC    5

// --- ES8311 helpers ---------------------------------------------------------
static uint8_t _t5_es8311_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    T5_LOG("I2C WR reg=0x%02X val=0x%02X err=%u", reg, val, err);
    return err;
}

// Configure ES8311 for DMIC input + I2S ADC output.
static void _t5_es8311_init_record() {
    T5_LOG("ES8311 record init start");

    _t5_es8311_write(0x44, 0x08);
    _t5_es8311_write(0x44, 0x08);

    _t5_es8311_write(0x01, 0x30);
    _t5_es8311_write(0x02, 0x00);
    _t5_es8311_write(0x03, 0x10);
    _t5_es8311_write(0x16, 0x24);
    _t5_es8311_write(0x04, 0x20);
    _t5_es8311_write(0x05, 0x00);
    _t5_es8311_write(0x0B, 0x00);
    _t5_es8311_write(0x0C, 0x00);
    _t5_es8311_write(0x10, 0x1F);
    _t5_es8311_write(0x11, 0x7F);

    _t5_es8311_write(0x00, 0x80);
    _t5_es8311_write(0x01, 0x3F);

    _t5_es8311_write(0x07, 0x00);
    _t5_es8311_write(0x08, 0xFF);
    _t5_es8311_write(0x06, 0x03);

    _t5_es8311_write(0x09, 0x0C);
    _t5_es8311_write(0x0A, 0x0C);

    _t5_es8311_write(0x13, 0x10);
    _t5_es8311_write(0x1B, 0x0A);
    _t5_es8311_write(0x1C, 0x6A);
    _t5_es8311_write(0x44, 0x58);   // ADC -> I2S SDP

    // DMIC mode ON (bit7=1) + 30 dB PGA (bits[3:0]=0xA).
    // BCLK is driven at 1.024 MHz so LMD4737 PDM mic is in spec.
    _t5_es8311_write(0x14, 0x8A);

    _t5_es8311_write(0x17, 0xFF);   // ADC digital volume max
    _t5_es8311_write(0x0E, 0x02);
    _t5_es8311_write(0x12, 0x00);
    _t5_es8311_write(0x0D, 0x01);
    _t5_es8311_write(0x15, 0x40);
    _t5_es8311_write(0x37, 0x08);

    T5_LOG("ES8311 record init done");
}

// Same playback sequence as T4 (kept local to avoid coupling to test_t4_codec.h).
static void _t5_es8311_init_playback() {
    T5_LOG("ES8311 playback init start");
    _t5_es8311_write(0x44, 0x08);
    _t5_es8311_write(0x44, 0x08);

    _t5_es8311_write(0x01, 0x30);
    _t5_es8311_write(0x02, 0x00);
    _t5_es8311_write(0x03, 0x10);
    _t5_es8311_write(0x16, 0x24);
    _t5_es8311_write(0x04, 0x20);
    _t5_es8311_write(0x05, 0x00);
    _t5_es8311_write(0x0B, 0x00);
    _t5_es8311_write(0x0C, 0x00);
    _t5_es8311_write(0x10, 0x1F);
    _t5_es8311_write(0x11, 0x7F);

    _t5_es8311_write(0x00, 0x80);
    _t5_es8311_write(0x01, 0x3F);

    _t5_es8311_write(0x07, 0x00);
    _t5_es8311_write(0x08, 0xFF);
    _t5_es8311_write(0x06, 0x03);

    _t5_es8311_write(0x09, 0x0C);
    _t5_es8311_write(0x0A, 0x0C);

    _t5_es8311_write(0x13, 0x10);
    _t5_es8311_write(0x1B, 0x0A);
    _t5_es8311_write(0x1C, 0x6A);
    _t5_es8311_write(0x44, 0x58);

    _t5_es8311_write(0x17, 0xBF);
    _t5_es8311_write(0x0E, 0x02);
    _t5_es8311_write(0x12, 0x00);
    _t5_es8311_write(0x14, 0x1A);
    _t5_es8311_write(0x0D, 0x01);
    _t5_es8311_write(0x15, 0x40);
    _t5_es8311_write(0x37, 0x08);
    _t5_es8311_write(0x45, 0x00);
    _t5_es8311_write(0x32, 0xBF);
    _t5_es8311_write(0x31, 0x00);
    T5_LOG("ES8311 playback init done");
}

static bool _t5_i2s_init_rx() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = T5_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = T5_BUF_SAMPLES,
        .use_apll             = false,  // legacy driver APLL is broken on S3
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan        = I2S_BITS_PER_CHAN_DEFAULT,
    };
    esp_err_t e = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    T5_LOG("i2s_driver_install(RX) => %d", (int)e);
    if (e != ESP_OK) return false;

    i2s_pin_config_t pins = {
        .mck_io_num   = PIN_I2S_MCLK,
        .bck_io_num   = PIN_I2S_BCLK,
        .ws_io_num    = PIN_I2S_LRCK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_I2S_DIN,
    };
    e = i2s_set_pin(I2S_NUM_0, &pins);
    T5_LOG("i2s_set_pin(RX) => %d (din=%d mclk=%d)", (int)e, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (e != ESP_OK) return false;

    T5_LOG("native I2S MCLK on GPIO%d @ %uHz", PIN_I2S_MCLK, (unsigned)T5_MCLK_HZ);
    return true;
}

static bool _t5_i2s_init_tx() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = T5_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = T5_BUF_SAMPLES,
        .use_apll             = false,  // legacy driver APLL is broken on S3
        .tx_desc_auto_clear   = true,
        .fixed_mclk           = 0,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan        = I2S_BITS_PER_CHAN_DEFAULT,
    };
    esp_err_t e = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    T5_LOG("i2s_driver_install(TX) => %d", (int)e);
    if (e != ESP_OK) return false;

    i2s_pin_config_t pins = {
        .mck_io_num   = PIN_I2S_MCLK,
        .bck_io_num   = PIN_I2S_BCLK,
        .ws_io_num    = PIN_I2S_LRCK,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };
    e = i2s_set_pin(I2S_NUM_0, &pins);
    T5_LOG("i2s_set_pin(TX) => %d (dout=%d mclk=%d)", (int)e, PIN_I2S_DOUT, PIN_I2S_MCLK);
    if (e != ESP_OK) return false;

    T5_LOG("native I2S MCLK on GPIO%d @ %uHz", PIN_I2S_MCLK, (unsigned)T5_MCLK_HZ);
    return true;
}

inline TestResult runTestT5(Display& disp, TestRunner& runner) {
    T5_LOG("START");

    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    Wire.beginTransmission(ES8311_ADDR);
    bool i2cOk = (Wire.endTransmission() == 0);
    T5_LOG("I2C probe 0x18 ok=%d", (int)i2cOk);
    if (!i2cOk) {
        const char* lines[] = { "I2C probe(0x18) FAIL" };
        disp.showTestScreen(5, "DMIC Record+Playback", lines, 1, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    // --- Allocate PSRAM mono buffer (5s x 15625 x 2B = ~156 KB) ------------
    const uint32_t totalSamples = (uint32_t)T5_SAMPLE_RATE * T5_RECORD_SEC;
    const size_t   bufBytes     = totalSamples * sizeof(int16_t);
    int16_t* monoBuf = (int16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    if (!monoBuf) {
        // Fallback to internal RAM (will likely fail at this size).
        monoBuf = (int16_t*)heap_caps_malloc(bufBytes, MALLOC_CAP_8BIT);
    }
    T5_LOG("alloc monoBuf=%p bytes=%u", monoBuf, (unsigned)bufBytes);
    if (!monoBuf) {
        const char* lines[] = { "Out of memory", "Need PSRAM enabled" };
        disp.showTestScreen(5, "DMIC Record+Playback", lines, 2, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    // --- Phase 1: Record ----------------------------------------------------
    // [DEBUG] Loop record+playback until operator confirms PASS (AP).
    //         Pressing BOOT on the verdict screen retries the cycle.
    bool verdict = false;
    uint32_t attempt = 0;
    do {
        attempt++;
        T5_LOG("=== attempt #%u ===", (unsigned)attempt);

    if (!_t5_i2s_init_rx()) {
        free(monoBuf);
        const char* lines[] = { "I2S RX init FAILED" };
        disp.showTestScreen(5, "DMIC Record+Playback", lines, 1, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }
    delay(50);
    _t5_es8311_init_record();
    delay(100);

    // Discard ~50 ms of warm-up samples.
    {
        int16_t warm[T5_BUF_SAMPLES * 2];
        size_t br = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t t0 = millis();
            i2s_read(I2S_NUM_0, warm, sizeof(warm), &br, portMAX_DELAY);
            T5_LOG("warm[%d] br=%u took=%ums", i, (unsigned)br, (unsigned)(millis()-t0));
        }
    }

    int16_t   stereoBuf[T5_BUF_SAMPLES * 2];
    uint32_t  recorded = 0;
    uint64_t  sumSq    = 0;
    int16_t   peak     = 0;
    bool      earlyAbort   = false;
    bool      earlyVerdict = false;
    uint32_t  lastUiMs = 0;

    T5_LOG("Recording %us @ %uHz...", (unsigned)T5_RECORD_SEC, (unsigned)T5_SAMPLE_RATE);

    uint32_t recStartMs = millis();
    uint32_t readCalls  = 0;
    uint32_t readBytes  = 0;
    uint32_t lastDiagMs = recStartMs;

    while (recorded < totalSamples && !earlyAbort) {
        size_t br = 0;
        esp_err_t e = i2s_read(I2S_NUM_0, stereoBuf, sizeof(stereoBuf),
                               &br, pdMS_TO_TICKS(200));
        readCalls++;
        readBytes += br;
        if (e != ESP_OK) {
            T5_LOG("i2s_read err=%d br=%u", (int)e, (unsigned)br);
            continue;
        }
        if (br == 0) {
            T5_LOG("i2s_read TIMEOUT br=0 (codec not clocking?)");
            continue;
        }
        size_t frames = br / 4;             // 16-bit stereo -> 4 B/frame
        if (recorded + frames > totalSamples) frames = totalSamples - recorded;

        for (size_t i = 0; i < frames; i++) {
            int32_t l = stereoBuf[i*2];
            int32_t r = stereoBuf[i*2 + 1];
            int32_t m = (l + r) / 2;
            monoBuf[recorded + i] = (int16_t)m;
            sumSq += (uint64_t)((int64_t)m * (int64_t)m);
            int16_t a = (m < 0) ? (int16_t)-m : (int16_t)m;
            if (a > peak) peak = a;
        }
        recorded += frames;

        uint32_t now = millis();
        if (now - lastDiagMs >= 1000) {
            uint32_t dt = now - lastDiagMs;
            T5_LOG("diag: t=%ums recorded=%u calls=%u bytes=%u rate~=%uHz",
                   (unsigned)(now-recStartMs), (unsigned)recorded,
                   (unsigned)readCalls, (unsigned)readBytes,
                   (unsigned)((readBytes / 4) * 1000U / dt));
            lastDiagMs = now;
            readCalls = 0;
            readBytes = 0;
        }
        if (now - lastUiMs > 200) {
            lastUiMs = now;
            uint32_t rms = (recorded > 0)
                            ? (uint32_t)sqrt((double)(sumSq / recorded))
                            : 0;
            uint32_t remainMs = (totalSamples - recorded) * 1000U / T5_SAMPLE_RATE;
            char l0[32], l1[32], l2[32], l3[32];
            snprintf(l0, sizeof(l0), "Attempt #%u", (unsigned)attempt);
            snprintf(l1, sizeof(l1), "Speak / sing for %ds", T5_RECORD_SEC);
            snprintf(l2, sizeof(l2), "Time left: %u.%us",
                     (unsigned)(remainMs / 1000),
                     (unsigned)((remainMs % 1000) / 100));
            snprintf(l3, sizeof(l3), "RMS=%u  Peak=%d",
                     (unsigned)rms, (int)peak);
            const char* lines[] = { l0, l1, "", l2, l3, "Recording..." };
            disp.showTestScreen(5, "DMIC Record", lines, 6, nullptr, nullptr);

            if (digitalRead(PIN_BOOT_BTN) == LOW) {
                delay(50); while (digitalRead(PIN_BOOT_BTN) == LOW) {}
                earlyAbort = true; earlyVerdict = false;
            }
        }
    }

    uint32_t rms = (recorded > 0)
                    ? (uint32_t)sqrt((double)(sumSq / recorded))
                    : 0;
    T5_LOG("Recording done: samples=%u rms=%u peak=%d",
           (unsigned)recorded, (unsigned)rms, (int)peak);

    i2s_driver_uninstall(I2S_NUM_0);

    if (earlyAbort) {
        // BOOT during record: retry instead of returning.
        T5_LOG("aborted by BOOT during record - retrying");
        (void)earlyVerdict;
        continue;
    }

    // --- Phase 2: Playback --------------------------------------------------
    if (!_t5_i2s_init_tx()) {
        free(monoBuf);
        const char* lines[] = { "I2S TX init FAILED" };
        disp.showTestScreen(5, "DMIC Playback", lines, 1, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }
    delay(50);
    _t5_es8311_init_playback();
    delay(50);

    digitalWrite(PIN_PA_CTRL, HIGH);
    delay(80);

    {
        char l0[32], l1[32];
        snprintf(l0, sizeof(l0), "Attempt #%u", (unsigned)attempt);
        snprintf(l1, sizeof(l1), "RMS=%u  Peak=%d",
                 (unsigned)rms, (int)peak);
        const char* lines[] = { l0,
                                "Playing back recording...",
                                "(speaker should reproduce voice)",
                                "",
                                l1,
                                "BOOT=stop early" };
        disp.showTestScreen(5, "DMIC Playback", lines, 6, nullptr, nullptr);
    }

    T5_LOG("Playing back %u samples...", (unsigned)totalSamples);
    int16_t  txBuf[T5_BUF_SAMPLES * 2];
    uint32_t played = 0;
    bool     pbAbort = false;
    while (played < totalSamples && !pbAbort) {
        uint32_t chunk = T5_BUF_SAMPLES;
        if (played + chunk > totalSamples) chunk = totalSamples - played;
        for (uint32_t i = 0; i < chunk; i++) {
            int16_t s = monoBuf[played + i];
            txBuf[i*2]     = s;
            txBuf[i*2 + 1] = s;
        }
        size_t bw = 0;
        i2s_write(I2S_NUM_0, txBuf, chunk * 4, &bw, portMAX_DELAY);
        played += chunk;

        if (digitalRead(PIN_BOOT_BTN) == LOW) {
            delay(50); while (digitalRead(PIN_BOOT_BTN) == LOW) {}
            pbAbort = true;
        }
    }
    T5_LOG("Playback done (played=%u abort=%d)",
           (unsigned)played, (int)pbAbort);

    // Tail flush so DMA empties before we kill the clocks.
    delay(80);
    digitalWrite(PIN_PA_CTRL, LOW);
    i2s_driver_uninstall(I2S_NUM_0);

    // --- Verdict screen -----------------------------------------------------
    bool autoPass = (rms > DMIC_RMS_THRESHOLD_VOICE);
    {
        char l0[32], l1[32], l2[32], l3[32];
        snprintf(l0, sizeof(l0), "Attempt #%u  Auto:%s",
                 (unsigned)attempt, autoPass ? "PASS" : "FAIL");
        snprintf(l1, sizeof(l1), "RMS=%u (thr=%u)",
                 (unsigned)rms, (unsigned)DMIC_RMS_THRESHOLD_VOICE);
        snprintf(l2, sizeof(l2), "Peak=%d  Samples=%u",
                 (int)peak, (unsigned)recorded);
        snprintf(l3, sizeof(l3), "AP=PASS  BOOT=retry");
        const char* lines[] = { "Did you hear yourself?",
                                l0, l1, l2, "",
                                l3 };
        disp.showTestScreen(5, "DMIC Result", lines, 6,
                            autoPass ? "PASS" : "FAIL",
                            "AP/BOOT");
    }
    verdict = runner.waitForVerdict();
    T5_LOG("verdict=%s rms=%u (attempt=%u)",
           verdict ? "PASS" : "FAIL/RETRY", (unsigned)rms, (unsigned)attempt);
    } while (!verdict);

    free(monoBuf);
    T5_LOG("END verdict=PASS attempts=%u", (unsigned)attempt);
    return TestResult::PASS;
}

