#pragma once
// T4 — ES8311 CODEC test
//   T4-A: I2C probe at 0x18 (auto pass/fail)
//   T4-B: I2S 1kHz sine sweep playback via PA (human listen confirmation)
//
// IDF 4.4 / Arduino-ESP32 stock SDK has a known bug on ESP32-S3 where
// i2s_set_pin cannot route MCLK through the GPIO matrix. As a workaround,
// we generate MCLK = 4.000 MHz on GPIO21 with LEDC, and tell I2S to NOT
// touch GPIO21 (mck_io_num = NO_CHANGE). Sample rate = 4MHz/256 = 15625 Hz.

#include "test_runner.h"
#include "config.h"
#include <Wire.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <math.h>

#define T4_DEBUG 1
#if T4_DEBUG
#define T4_LOG(fmt, ...) Serial.printf("[T4] " fmt "\n", ##__VA_ARGS__)
#else
#define T4_LOG(fmt, ...)
#endif

#define ES8311_ADDR 0x18

static uint8_t _es8311_write(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    uint8_t err = Wire.endTransmission();
    T4_LOG("I2C WR reg=0x%02X val=0x%02X err=%u", reg, val, err);
    return err;
}

static bool _es8311_read(uint8_t reg, uint8_t& outVal) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1) != 1) return false;
    outVal = Wire.read();
    T4_LOG("I2C RD reg=0x%02X val=0x%02X", reg, outVal);
    return true;
}

static void _es8311_dump_key_regs() {
    const uint8_t regs[] = {0x00, 0x01, 0x06, 0x07, 0x08, 0x09, 0x0A,
                             0x0D, 0x0E, 0x12, 0x14, 0x31, 0x32};
    for (size_t i = 0; i < sizeof(regs); i++) {
        uint8_t v = 0;
        (void)_es8311_read(regs[i], v);
    }
}

static void _es8311_init_playback() {
    T4_LOG("ES8311 init start");
    _es8311_write(0x44, 0x08);
    _es8311_write(0x44, 0x08);

    _es8311_write(0x01, 0x30);
    _es8311_write(0x02, 0x00);
    _es8311_write(0x03, 0x10);
    _es8311_write(0x16, 0x24);
    _es8311_write(0x04, 0x20);
    _es8311_write(0x05, 0x00);
    _es8311_write(0x0B, 0x00);
    _es8311_write(0x0C, 0x00);
    _es8311_write(0x10, 0x1F);
    _es8311_write(0x11, 0x7F);

    _es8311_write(0x00, 0x80);
    _es8311_write(0x01, 0x3F);

    // MCLK=4.000MHz, FS=15.625kHz → LRCK_DIV=256
    _es8311_write(0x07, 0x00);
    _es8311_write(0x08, 0xFF);
    _es8311_write(0x06, 0x03);

    _es8311_write(0x09, 0x0C);
    _es8311_write(0x0A, 0x0C);

    _es8311_write(0x13, 0x10);
    _es8311_write(0x1B, 0x0A);
    _es8311_write(0x1C, 0x6A);
    _es8311_write(0x44, 0x58);

    _es8311_write(0x17, 0xBF);
    _es8311_write(0x0E, 0x02);
    _es8311_write(0x12, 0x00);
    _es8311_write(0x14, 0x1A);
    _es8311_write(0x0D, 0x01);
    _es8311_write(0x15, 0x40);
    _es8311_write(0x37, 0x08);
    _es8311_write(0x45, 0x00);
    _es8311_write(0x32, 0xBF);
    _es8311_write(0x31, 0x00);

    T4_LOG("ES8311 init done, dump key regs");
    _es8311_dump_key_regs();
}

#define T4_SAMPLE_RATE  16000
#define T4_BUF_SAMPLES  512

static i2s_chan_handle_t _t4_tx_handle = nullptr;

static bool _t4_i2s_init() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = 4;
    chan_cfg.dma_frame_num = T4_BUF_SAMPLES;
    chan_cfg.auto_clear    = true;
    esp_err_t e = i2s_new_channel(&chan_cfg, &_t4_tx_handle, nullptr);
    T4_LOG("i2s_new_channel(TX) => %d", (int)e);
    if (e != ESP_OK) return false;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(T4_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = (gpio_num_t)PIN_I2S_MCLK,
            .bclk = (gpio_num_t)PIN_I2S_BCLK,
            .ws   = (gpio_num_t)PIN_I2S_LRCK,
            .dout = (gpio_num_t)PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    e = i2s_channel_init_std_mode(_t4_tx_handle, &std_cfg);
    T4_LOG("i2s_channel_init_std_mode => %d (mclk=%d bclk=%d lrck=%d dout=%d)",
           (int)e, PIN_I2S_MCLK, PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT);
    if (e != ESP_OK) return false;

    e = i2s_channel_enable(_t4_tx_handle);
    T4_LOG("i2s_channel_enable(TX) => %d", (int)e);
    if (e != ESP_OK) return false;

    T4_LOG("i2s_std TX up: Fs=%uHz MCLK=GPIO%d (Fs*256=%uHz)",
           (unsigned)T4_SAMPLE_RATE, PIN_I2S_MCLK,
           (unsigned)(T4_SAMPLE_RATE * 256U));
    return true;
}

static void _t4_i2s_deinit() {
    if (_t4_tx_handle) {
        i2s_channel_disable(_t4_tx_handle);
        i2s_del_channel(_t4_tx_handle);
        _t4_tx_handle = nullptr;
    }
}

inline TestResult runTestT4(Display& disp, TestRunner& runner) {
    T4_LOG("START");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    T4_LOG("Wire.begin SDA=%d SCL=%d", PIN_I2C_SDA, PIN_I2C_SCL);

    Wire.beginTransmission(ES8311_ADDR);
    bool i2cOk = (Wire.endTransmission() == 0);
    T4_LOG("I2C probe 0x18 ok=%d", (int)i2cOk);

    {
        char probe[32];
        snprintf(probe, sizeof(probe), "I2C probe(0x18)... %s", i2cOk ? "OK" : "FAIL");
        const char* lines[] = { probe };
        disp.showTestScreen(4, "ES8311 CODEC", lines, 1,
                            i2cOk ? "PASS" : "FAIL",
                            i2cOk ? "AP=Next" : "AP=Skip");
    }
    if (!i2cOk) { runner.waitForAP(); return TestResult::FAIL; }
    runner.waitForAP();

    bool i2sOk = _t4_i2s_init();
    T4_LOG("i2sOk=%d, waiting 50ms", (int)i2sOk);
    delay(50);
    if (i2sOk) _es8311_init_playback();

    pinMode(PIN_PA_CTRL, OUTPUT);
    digitalWrite(PIN_PA_CTRL, HIGH);
    T4_LOG("PA_CTRL=%d -> HIGH (rb=%d)", PIN_PA_CTRL, digitalRead(PIN_PA_CTRL));
    delay(100);

    if (!i2sOk) {
        const char* lerr[] = { "I2S init FAILED" };
        disp.showTestScreen(4, "ES8311 CODEC-Play", lerr, 1, "FAIL", "AP=Next");
        runner.waitForAP();
        return TestResult::FAIL;
    }

    T4_LOG("SWEEP: 500Hz~8kHz, AP=PASS BOOT=FAIL");
    static const float freqs[] = {500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f};
    static const char* labels[] = {"500 Hz","1000 Hz","2000 Hz","4000 Hz","8000 Hz"};
    const int nFreqs = 5;

    int idx = 0;
    bool exitSweep = false, verdict = false;
    static int16_t buf[T4_BUF_SAMPLES * 2];
    static uint32_t phase = 0;

    while (!exitSweep) {
        float f = freqs[idx];
        char l1[32];
        snprintf(l1, sizeof(l1), "Playing: %s", labels[idx]);
        const char* lines[] = { "PA Amp: ON", l1, "", "AP=PASS  BOOT=FAIL" };
        disp.showTestScreen(4, "CODEC DEBUG SWEEP", lines, 4, nullptr, nullptr);
        T4_LOG("sweep freq=%.0fHz", f);

        uint32_t total = T4_SAMPLE_RATE;
        uint32_t written = 0;
        while (written < total && !exitSweep) {
            uint32_t chunk = T4_BUF_SAMPLES;
            if (written + chunk > total) chunk = total - written;
            for (uint32_t i = 0; i < chunk; i++) {
                int16_t s = (int16_t)(16000.0f * sinf(2.0f * (float)M_PI * f * (float)phase / (float)T4_SAMPLE_RATE));
                phase++;
                buf[i*2] = s; buf[i*2+1] = s;
            }
            size_t bw = 0;
            i2s_channel_write(_t4_tx_handle, buf, chunk * 4, &bw, portMAX_DELAY);
            written += chunk;
            if (digitalRead(PIN_AP_BTN) == LOW) {
                delay(50); while (digitalRead(PIN_AP_BTN) == LOW) {}
                exitSweep = true; verdict = true;
            } else if (digitalRead(PIN_BOOT_BTN) == LOW) {
                delay(50); while (digitalRead(PIN_BOOT_BTN) == LOW) {}
                exitSweep = true; verdict = false;
            }
        }
        if (!exitSweep) idx = (idx + 1) % nFreqs;
    }

    digitalWrite(PIN_PA_CTRL, LOW);
    _t4_i2s_deinit();
    T4_LOG("END verdict=%s", verdict ? "PASS" : "FAIL");
    return verdict ? TestResult::PASS : TestResult::FAIL;
}
