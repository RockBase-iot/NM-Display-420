#pragma once

// ─── Firmware version ────────────────────────────────────────────────────────
#define FW_VERSION  "v1.3"
#define BOARD_NAME  "NM Display 4.2 Inch"

// ─── EPD (SPI0 / VSPI) ───────────────────────────────────────────────────────
#define PIN_EPD_SCK   2
#define PIN_EPD_MOSI  1
#define PIN_EPD_MISO  10  // NC on board (display is write-only)
#define PIN_EPD_CS    3
#define PIN_EPD_DC    4
#define PIN_EPD_RST   5
#define PIN_EPD_BUSY  6

// ─── SD card + LoRa (FSPI, shared bus) ───────────────────────────────────────
#define PIN_SD_CLK    9
#define PIN_SD_MOSI   10   // CMD
#define PIN_SD_MISO   11   // DAT0
#define PIN_SD_CS     7

#define PIN_LORA_NSS  8    // LoRa chip-select (CS), shares FSPI bus with SD
#define PIN_LORA_RST  12
#define PIN_LORA_BUSY 13
#define PIN_LORA_DIO1 14   // Not used in SPI connectivity test

// ─── I2S (ES8311 CODEC) ──────────────────────────────────────────────────────
#define PIN_I2S_MCLK  21
#define PIN_I2S_BCLK  15
#define PIN_I2S_LRCK  17
#define PIN_I2S_DOUT  16
#define PIN_I2S_DIN   18

// ─── I2C (AHT20 + ES8311 shared bus) ─────────────────────────────────────────
#define PIN_I2C_SDA   39
#define PIN_I2C_SCL   38
#define PIN_TEMP_CTL  40   // AHT20 power enable (HIGH = on)

// ─── GPIO ────────────────────────────────────────────────────────────────────
#define PIN_PA_CTRL   41   // External amplifier enable (HIGH = on)
#define PIN_BOOT_BTN  0    // BOOT key, external pull-up, pressed = LOW (RTC GPIO)
#define PIN_AP_BTN    45   // AP key,   external pull-up, pressed = LOW
#define PIN_LED       47   // WS2812 data pin (RMT / NeoPixel driver)

// ─── WS2812 ──────────────────────────────────────────────────────────────────
#define WS2812_COUNT  1    // Number of onboard WS2812 LEDs

// ─── Feature switches ─────────────────────────────────────────────────────────
#define BATTERY_ADC_AVAILABLE  0   // 0 = SKIP (HW not populated), 1 = test

// ─── DMIC recording thresholds ────────────────────────────────────────────────
#define DMIC_RMS_THRESHOLD_VOICE     150   // Phase-1: operator voice
#define DMIC_RMS_THRESHOLD_LOOPBACK   50   // Phase-2: speaker loopback
