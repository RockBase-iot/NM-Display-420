# T5 DMIC 调试记录

日期：2026-04-27
板：NM-Display-420（ESP32-S3 + ES8311 + LMD4737T261 PDM mic + NS4150B PA）
框架：Arduino + PlatformIO，espressif32 6.13.0，legacy `driver/i2s.h`

---

## 硬件拓扑（已通过原理图核对）

| 网络 | ESP32-S3 | ES8311 | LMD4737 |
|---|---|---|---|
| I2S_MCLK | GPIO21 | pin2 MCLK | — |
| I2S_SCLK (= BCLK = DMIC_SCL) | GPIO15 | pin6 SCLK/DMIC_SCL | pin4 CLK（经 R22 0Ω）|
| I2S_LRCK | GPIO17 | pin8 LRCK | pin2 L/R |
| I2S_ASDOUT (DIN to ESP) | GPIO16 | pin7 ASDOUT | — |
| I2S_DSDIN (DOUT from ESP) | GPIO18 | pin9 DSDIN | — |
| DMIC_SDA (= ES8311 MIC1P) | — | pin18 MIC1P/DMIC_SDA | pin3 DATA（经 R21 0Ω）|
| I2C SDA/SCL | GPIO8/9 | AU_SDA/SCL | — |
| PA_CTRL | GPIO41 | — | — |

**关键事实**：LMD4737 PDM 数字麦的 CLK 与 ES8311 BCLK 共网络，DATA 与 ES8311 MIC1P/DMIC_SDA 共脚。这是 ES8311 + 外置 PDM mic 的标准拓扑，硬件无需改。

LMD4737 spec：CLK 工作范围 1 MHz ~ 3.25 MHz。

---

## 已确认正常的部分

- I2C 通讯 0x18 OK
- T4 codec 播放 OK（Fs=15625, MCLK=4MHz, 老配置稳定，原始 LEDC 已替换为原生 I2S MCLK）
- 录音 RMS 在 baseline 配置下能拿到非零数据（rms~314 peak~2185）
- ES8311 寄存器 init 序列复用 espressif esp_codec_dev 推荐值

---

## 调试时间线（按尝试顺序）

### 尝试 1：DMIC 模式 + Fs=15625 (BCLK=500kHz)
- 录音 RMS=314 peak=2185，回放有"杂音中带人声痕迹"
- 问题：BCLK=500kHz 低于 LMD4737 spec 下限 1MHz，PDM 输出不稳定→污染信号

### 尝试 2：抬 BCLK 到 2MHz（Fs=62500，MCLK=16MHz，LEDC 输出 MCLK）
- 用 LEDC 在 GPIO21 输出 16MHz 作为 MCLK
- 实测 GPIO21 周期 60ns（16MHz）✓
- 但回放完全无声
- 录音耗时 25s 收 312500 样本（实际速率 ~12.5kHz，5× 偏差），ES8311 寄存器仍是 Fs=15625 配置

### 尝试 3：原生 I2S MCLK（去 LEDC）+ APLL，Fs=62500
- `mck_io_num = PIN_I2S_MCLK` 让 I2S 外设输出 MCLK
- IO21 周期变 62us（即 ~16kHz）→ MCLK 没真正按 16MHz 出，实际是 LRCK 串过来了，APLL 算不出
- 录音慢、无声

### 尝试 4：APLL 开 + Fs=16000 + bits_per_chan=32
- 想用 32-bit slot 把 BCLK 抬到 1.024MHz
- 录音 9s 收 80000 样本，rms=0 peak=0（数据全 0）
- 32-bit slot 在 legacy 驱动里 16-bit 数据布局不对，读出来低 16 位是 0

### 尝试 5：APLL 开 + Fs=32000 + 16-bit slot
- BCLK 名义 1.024MHz
- 录音 16s 收 160000 样本（实际 ~10kHz）
- rms=250 peak=888，回放只有底噪

### 尝试 6：APLL 开 + Fs=48000 + 16-bit slot（音频圣点 12.288MHz MCLK）
- **示波器实测**：IO21=12.288MHz ✓ / GPIO15=1.536MHz ✓ / GPIO17=48kHz ✓（**硬件时钟全对！**）
- 但 `i2s_read` 实际 frame 速率 ~10kHz（rate~=10108Hz 持续 24s）
- rms=1225 peak=5425，回放 5s 完整 240000 样本 = 48kHz，能听到"嘶嘶声"
- **结论**：legacy `driver/i2s.h` + APLL 在 ESP32-S3 上的 RX DMA 时序与外设输出 LRCK 解耦，外设端时钟正确但 DMA 写 FIFO 节奏错。这是已知 bug。

### 尝试 7：关 APLL，PLL_F160M 整数分频，Fs=31250 MCLK=8MHz
- 期望 160M/20=8MHz 干净
- 录音 12s 收 156250 样本（实际 ~12kHz）→ 还是 RX DMA 慢
- rms=46 peak=423，回放完全无声
- 即使关 APLL，legacy RX 在 Fs > ~16k 的 ESP32-S3 上依然失败

### 尝试 8：Fs=15625 + bits_per_chan=32（再试 32-bit slot）
- 想保住 RX 干净频率，又抬 BCLK 到 1MHz
- 录音 7s 收 78125 样本（rate~11kHz），rms=0 peak=0（数据全 0）
- 同尝试 4，32-bit slot 数据布局问题

### 尝试 9（当前 baseline）：Fs=15625，16-bit slot，DMIC ON
- BCLK=500kHz（低于 spec），但是这个组合 RX 时基真实，能拿到非 0 数据
- 当前回放仍然只有杂音，没有清晰人声

---

## 根因诊断

1. **legacy `driver/i2s.h` 在 ESP32-S3 上对 RX 的支持有缺陷**：
   - 外设输出 MCLK/BCLK/LRCK 的 GPIO 频率正确（示波器证实）
   - 但 RX DMA 把 frame 写入用户缓冲的速率与 LRCK 解耦，停在 ~10~12kHz
   - 这个限制使得"抬 BCLK 到 LMD4737 工作区间"的所有方案都失败
2. **`bits_per_chan = 32` 在 16-bit `bits_per_sample` 下，legacy 驱动数据布局错误**：读到全 0
3. **因此 baseline (Fs=15625, BCLK=500kHz) 是 legacy 驱动唯一能跑通的配置**，但 LMD4737 在 500kHz CLK 下输出畸变 PDM，再经 ES8311 解码就成噪声

---

## 已修改文件

- `src/tests/test_t5_dmic.h`：当前停在 baseline (Fs=15625, MCLK=4MHz, 16-bit slot, DMIC ON, 30dB PGA)，含 i2s_read 速率 diag 日志、boot 重试循环
- `src/tests/test_t4_codec.h`：MCLK 已从 LEDC hack 改成原生 I2S MCLK，APLL=false（编译通过，T4 仍 OK）
- `src/tests/test_t2_led.h`：每色 1s（应用户要求）
- `src/config.h`：DOUT=18, DIN=16（pin 反向修复，T4 调通的关键）

---

## 明天的方向：升级到 IDF 5.x `driver/i2s_std.h`

新版 API 在 ESP32-S3 上 RX DMA 时序与外设 LRCK 同步，APLL/PLL 路径都修过。预期：
- Fs=48000 / MCLK=12.288MHz / BCLK=1.536MHz 真正能跑（外设和 DMA 一致）
- LMD4737 拿到 spec 内 CLK，PDM 输出干净
- ES8311 DMIC 解码出清晰人声

### 升级步骤草案
1. 在 `platformio.ini` 切到 espressif32 6.13.0 自带的 IDF 版本（已是 4.4.x 系列封装，但 Arduino 3.x 平台会带 IDF 5.x）。需要确认 `framework = arduino` 下能否直接 include `driver/i2s_std.h`，否则切到 `framework = arduino, espidf` 双框架
2. 把 `_t5_i2s_init_rx()` / `_t5_i2s_init_tx()` 重写为 `i2s_new_channel` + `i2s_channel_init_std_mode` 模式
3. RX 用 `i2s_channel_read`（带 timeout）替代 `i2s_read`
4. T4 同步迁移以保持代码一致
5. 验证：示波器 + diag 日志 rate~ 应等于 Fs

### 备选方案（如 i2s_std 也不行）
- **路线 3（改硬件）**：剪 R21/R22，LMD4737 CLK/DATA 改接 ESP 独立 GPIO，用 ESP32-S3 PDM_RX 外设直接读 PDM，完全绕开 ES8311 ADC

---

## 关键日志（尝试 6，硬件时钟正确但 RX DMA 慢，最有诊断价值）

```
[T5] Recording 5s @ 48000Hz...
[T5] diag: t=1018ms recorded=9216 calls=18 bytes=36864 rate~=9053Hz
[T5] diag: t=2031ms recorded=19456 calls=20 bytes=40960 rate~=10108Hz
... (持续 24s)
[T5] Recording done: samples=240000 rms=1225 peak=5425
[T5] Playing back 240000 samples...
[T5] Playback done (played=240000 abort=0)   ← 5s 内完成，TX 速率正确
```

示波器：IO21=12.288MHz / GPIO15=1.536MHz / GPIO17=48kHz 全对。
