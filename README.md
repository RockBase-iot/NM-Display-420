# NM-Display-420
The ESP32-S3 4.2 inch E-ink display. 

## Factory Test Sequence

| Test | Item            | Description                              |
|------|-----------------|------------------------------------------|
| T0   | System Startup  | Serial init, EPD init, welcome screen    |
| T1   | EPD Display     | White / Black / Red fill + text demo     |
| T2   | WS2812 RGB LED  | RED > GREEN > BLUE > WHITE cycle         |
| T3   | Buttons         | AP key and BOOT key press detection      |
| T4   | ES8311 CODEC    | I2C probe (0x18) + I2S 1 kHz tone       |
| T5   | DMIC Mic        | LMD4737 voice recording + RMS check     |
| T6   | AHT20 Sensor    | Temperature & humidity via I2C           |
| T7   | Battery ADC     | ADC voltage reading (skip if not fitted) |
| T8   | WiFi Scan       | 2.4 GHz AP scan, expect ≥1 network       |
| T9   | SD Card R/W     | FSPI mount, write & read verify          |
| T10  | LoRa SPI Bus    | Reset chip, check BUSY low               |
| T11  | Summary         | Overall PASS / FAIL report               |
