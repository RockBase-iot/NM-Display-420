#pragma once
// T5 — LMD4737 PDM DMIC recording test (Phase-1: voice).
//
// Path: LMD4737 (PDM) → ES8311 DMIC_CLK/DAT → ES8311 PDM→PCM → I2S ADC
//       → ESP32 DIN (GPIO16, PIN_I2S_DIN).
//
// Reuses the same MCLK=4MHz LEDC trick + same I2S clocking as T4
// (sample_rate=15625Hz, 16-bit, stereo frame). We install I2S in RX mode
// only, set ES8311 register 0x14 to enable DMIC input (bit6=DMIC_ON),
// power up the ADC path, then stream-read for ~2s computing RMS on the fly.
//
// PA_CTRL is held LOW so the speaker does not feed back into the mic.
// Phase-2 (loopback playback while recording) is left as TODO.

#include "test_runner.h"
#include "config.h"
#include <Wire.h>
#include <driver/i2s.h>
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
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

#define T5_SAMPLE_RATE   15625
#define T5_MCLK_HZ       4000000
#define T5_BUF_SAMPLES   512
#define T5_RECORD_SEC    2

// ─── ES8311 helpers (local copies; T4 may not be linked yet) ─────────────────
static uint8_t _t5_es8311_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    T5_LOG("I2C WR reg=0x%02X val=0x%02X err=%u", reg, val, err);
    return err;
}

// Configure ES8311 for DMIC input + I2S ADC output. Mirrors the T4 register
// sequence but enables DMIC (reg 0x14 bit6) and powers the ADC path.
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

    // MCLK=4MHz, FS=15.625kHz → LRCK_DIV=256
    _t5_es8311_write(0x07, 0x00);
    _t5_es8311_write(0x08, 0xFF);
    _t5_es8311_write(0x06, 0x03);

    _t5_es8311_write(0x09, 0x0C);
    _t5_es8311_write(0x0A, 0x0C);

    // ADC chain
    _t5_es8311_write(0x13, 0x10);
    _t5_es8311_write(0x1B, 0x0A);
    _t5_es8311_write(0x1C, 0x6A);
    _t5_es8311_write(0x44, 0x58);   // ADC -> I2S SDP

    // DMIC ENABLE: reg 0x14 bit[6]=DMICON, bit[3:0]=PGA gain (kept at 0xA = 30dB)
    _t5_es8311_write(0x14, 0x5A);   // 0x5A = DMIC on + 30dB analog gain bits

    _t5_es8311_write(0x17, 0xBF);   // ADC volume ~ max
    _t5_es8311_write(0x0E, 0x02);   // ADC analog power
    _t5_es8311_write(0x12, 0x00);   // power on (DAC also; harmless)
    _t5_es8311_write(0x0D, 0x01);
    _t5_es8311_write(0x15, 0x40);
    _t5_es8311_write(0x37, 0x08);

    T5_LOG("ES8311 record init done");
}

static void _t5_setup_mclk_ledc() {
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_1_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = T5_MCLK_HZ,
        .clk_cfg         = LEDC_USE_APB_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .gpio_num   = PIN_I2S_MCLK,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 1,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);

    gpio_set_direction((gpio_num_t)PIN_I2S_MCLK, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal((gpio_num_t)PIN_I2S_MCLK,
                                    LEDC_LS_SIG_OUT0_IDX, false, false);
    T5_LOG("LEDC MCLK on GPIO%d @ %uHz", PIN_I2S_MCLK, (unsigned)T5_MCLK_HZ);
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
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
        .mclk_multiple        = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan        = I2S_BITS_PER_CHAN_DEFAULT,
    };
    esp_err_t e = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr);
    T5_LOG("i2s_driver_install(RX) => %d", (int)e);
    if (e != ESP_OK) return false;

    i2s_pin_config_t pins = {
        .mck_io_num   = I2S_PIN_NO_CHANGE,
        .bck_io_num   = PIN_I2S_BCLK,
        .ws_io_num    = PIN_I2S_LRCK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_I2S_DIN,
    };
    e = i2s_set_pin(I2S_NUM_0, &pins);
    T5_LOG("i2s_set_pin => %d (bclk=%d lrck=%d din=%d mclk=LEDC@GPIO%d)",
           (int)e, PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DIN, PIN_I2S_MCLK);
    if (e != ESP_OK) return false;

    _t5_setup_mclk_ledc();
    return true;
}

inline TestResult runTestT5(Display& disp, TestRunner& runner) {
    T5_LOG("START");

    // PA off so speaker output cannot pollute the mic during voice phase.
    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, LOW);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    Wire.beginTransmission(ES8311_ADDR);
    bool i2cOk = (Wire.endTransmission() == 0);
    T5_LOG("I2C probe 0x18 ok=%d", (int)i2cOk);
    if (!i2cOk) {
        const char* lines[] = { "I2C probe(0x18) FAIL" };
        disp.showTestScreen(5, "LMD4737 DMIC Mic", lines, 1, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    bool i2sOk = _t5_i2s_init_rx();
    delay(50);
    if (!i2sOk) {
        const char* lines[] = { "I2S RX init FAILED" };
        disp.showTestScreen(5, "LMD4737 DMIC Mic", lines, 1, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    _t5_es8311_init_record();
    delay(100);

    // Discard ~50ms of warm-up samples (DC settle, ALC ramp).
    static int16_t warmBuf[T5_BUF_SAMPLES * 2];
    {
        size_t br = 0;
        for (int i = 0; i < 4; i++) {
            i2s_read(I2S_NUM_0, warmBuf, sizeof(warmBuf), &br, portMAX_DELAY);
        }
    }

    // ─── Recording loop with live RMS display ──────────────────────────────
    const uint32_t totalSamples = (uint32_t)T5_SAMPLE_RATE * T5_RECORD_SEC;
    uint32_t recorded = 0;
    uint64_t sumSq = 0;     // accumulate sum of squares (mono = avg L+R)
    uint32_t sampleCount = 0;
    int16_t peak = 0;

    static int16_t recBuf[T5_BUF_SAMPLES * 2];
    uint32_t lastUiMs = 0;
    bool earlyAbort = false;
    bool earlyVerdict = false;

    T5_LOG("Recording %us @ %uHz...", (unsigned)T5_RECORD_SEC, (unsigned)T5_SAMPLE_RATE);

    while (recorded < totalSamples && !earlyAbort) {
        size_t br = 0;
        esp_err_t e = i2s_read(I2S_NUM_0, recBuf, sizeof(recBuf), &br, pdMS_TO_TICKS(200));
        if (e != ESP_OK || br == 0) {
            T5_LOG("i2s_read err=%d br=%u", (int)e, (unsigned)br);
            continue;
        }
        size_t frames = br / 4;             // 16-bit stereo → 4 bytes/frame
        for (size_t i = 0; i < frames; i++) {
            int32_t l = recBuf[i*2];
            int32_t r = recBuf[i*2 + 1];
            int32_t m = (l + r) / 2;
            sumSq += (uint64_t)((int64_t)m * (int64_t)m);
            sampleCount++;
            int16_t a = (m < 0) ? (int16_t)-m : (int16_t)m;
            if (a > peak) peak = a;
        }
        recorded += frames;

        // UI refresh ~5 Hz
        uint32_t now = millis();
        if (now - lastUiMs > 200) {
            lastUiMs = now;
            uint32_t rms = (sampleCount > 0)
                            ? (uint32_t)sqrt((double)(sumSq / sampleCount))
                            : 0;
            uint32_t remainMs = (totalSamples - recorded) * 1000U / T5_SAMPLE_RATE;

            char l1[32], l2[32], l3[32];
            snprintf(l1, sizeof(l1), "Speak / clap at the mic");
            snprintf(l2, sizeof(l2), "Time left: %u.%us",
                     (unsigned)(remainMs / 1000), (unsigned)((remainMs % 1000) / 100));
            snprintf(l3, sizeof(l3), "RMS=%u  Peak=%d", (unsigned)rms, (int)peak);
            const char* lines[] = { l1, "", l2, l3, "", "Recording..." };
            disp.showTestScreen(5, "LMD4737 DMIC", lines, 6, nullptr, nullptr);

            if (digitalRead(PIN_AP_BTN) == LOW) {
                delay(50); while (digitalRead(PIN_AP_BTN) == LOW) {}
                earlyAbort = true; earlyVerdict = true;
            } else if (digitalRead(PIN_BOOT_BTN) == LOW) {
                delay(50); while (digitalRead(PIN_BOOT_BTN) == LOW) {}
                earlyAbort = true; earlyVerdict = false;
            }
        }
    }

    uint32_t rms = (sampleCount > 0)
                    ? (uint32_t)sqrt((double)(sumSq / sampleCount))
                    : 0;
    T5_LOG("Recording done: samples=%u rms=%u peak=%d",
           (unsigned)sampleCount, (unsigned)rms, (int)peak);

    // Auto verdict: PASS if RMS exceeds voice threshold.
    bool autoPass = (rms > DMIC_RMS_THRESHOLD_VOICE) && !earlyAbort;
    bool verdict;
    if (earlyAbort) {
        verdict = earlyVerdict;
    } else {
        verdict = autoPass;
    }

    // ─── Result screen + manual override ───────────────────────────────────
    {
        char l1[32], l2[32], l3[32];
        snprintf(l1, sizeof(l1), "RMS=%u (thr=%u)",
                 (unsigned)rms, (unsigned)DMIC_RMS_THRESHOLD_VOICE);
        snprintf(l2, sizeof(l2), "Peak=%d  Samples=%u",
                 (int)peak, (unsigned)sampleCount);
        snprintf(l3, sizeof(l3), "Auto: %s",
                 verdict ? "PASS" : "FAIL");
        const char* lines[] = { "Recording finished.",
                                l1, l2, l3, "",
                                "AP=PASS  BOOT=FAIL" };
        disp.showTestScreen(5, "LMD4737 DMIC", lines, 6,
                            verdict ? "PASS" : "FAIL",
                            "AP/BOOT");
    }

    bool manual = runner.waitForVerdict();
    verdict = manual;

    i2s_driver_uninstall(I2S_NUM_0);
    T5_LOG("END verdict=%s rms=%u", verdict ? "PASS" : "FAIL", (unsigned)rms);
    return verdict ? TestResult::PASS : TestResult::FAIL;
}
