# NM-Display-420 出厂测试固件方案 v1.3

## 1. 概述

本固件用于 NM-Display-420 板的**出厂全功能测试**，覆盖板上所有主要硬件，通过按键逐步推进测试流程，测试结果实时显示在 E-Paper 屏幕上，同时通过串口输出日志供上位机采集。

**技术栈**：PlatformIO + Arduino 框架（board: `lilygo-t-display-s3`）

---

## 2. 硬件清单（待测项）

| 编号 | 模块 | 接口 | 引脚 / 地址 |
|------|------|------|------------|
| H1 | E-Paper 显示屏（400×300，三色） | SPI0 | SCK=2, MOSI=1, MISO=10(NC), CS=3, DC=4, RST=5, BUSY=6 |
| H2 | 板载 WS2812 RGB LED | RMT/单总线 | GPIO47（WS2812 数据引脚，需 RMT 或 NeoPixel 驱动）|
| H3 | BOOT 按键 | GPIO | GPIO0（外部上拉，按下=LOW，RTC GPIO） |
| H4 | AP 按键 | GPIO | GPIO45（外部上拉，按下=LOW） |
| H5 | AHT20 温湿度传感器 | I2C | SDA=39, SCL=38, 电源使能=GPIO40 |
| H6 | 电池电压 ADC | ADC | A0（100kΩ+100kΩ分压）**⚠️ 当前版本板子未接，占位** |
| H7 | WiFi | 内置 | ESP32-S3 内置 |
| H8 | ES8311 CODEC | I2C + I2S | I2C addr=0x18, SDA=39, SCL=38；MCLK=21, BCLK=15, LRCK=17, DOUT=16, DIN=18 |
| H9 | LMD4737 DMIC | PDM（经 ES8311） | DMIC_CLK/DAT 由 ES8311 内部驱动；ESP32 经 I2S DIN（GPIO18）接收 |
| H10 | 喇叭功放使能 | GPIO | PA_CTRL=GPIO41（HIGH=使能，LOW=关断）|
| H11 | TF/SD 卡 | SPI1 | CLK=9, CMD/MOSI=10, DAT0/MISO=11, CS=7 |

> **注意事项**：
> - H5 AHT20 与 H8 ES8311 共用 I2C 总线（SDA=39, SCL=38）
> - H1 EPD 使用 SPI0（SCK=2），H11 SD 卡使用独立 SPI1（CLK=9），两路 SPI 不冲突
> - 播放音频前必须先拉高 PA_CTRL（GPIO41）使能外部功放

---

## 3. 按键操作约定

| 按键 | 功能 |
|------|------|
| **AP 键（GPIO45）** | 确认 / 进入下一项 / 标记 PASS |
| **BOOT 键（GPIO0）** | 标记当前项失败 |

- **自动测试项**：完成后自动判断并显示 PASS/FAIL，按 **AP 键** 进入下一项。
- **人工判断项**：屏幕显示提示，**AP 键 = PASS，BOOT 键 = FAIL**。

---

## 4. 测试流程总览

```
上电
  │
  ▼
[T0]  系统启动 & 显示首页
  │   Serial 初始化，EPD 初始化，显示欢迎页，等待 AP 键
  ▼
[T1]  EPD 显示测试（白/黑/红三色全屏 + 文字渲染）
  │   人工判断 → AP=PASS / BOOT=FAIL
  ▼
[T2]  WS2812 RGB LED 测试（依次显示红/绿/蓝/白/关）
  │   人工判断 → AP=PASS / BOOT=FAIL
  ▼
[T3]  按键测试（依次检测 AP 键、BOOT 键被按下）
  │   自动判断，超时 10s/键 → FAIL
  ▼
[T4]  AHT20 传感器测试（I2C + 读值范围校验）
  │   自动判断
  ▼
[T5]  电池电压 ADC 测试（占位，当前板未接，自动 SKIP）
  │   不计入 PASS/FAIL
  ▼
[T6]  WiFi 扫描测试（扫描 AP 数量 ≥ 1）
  │   自动判断
  ▼
[T7]  SD 卡测试（SPI1 初始化 + 挂载 + 读写验证）
  │   自动判断
  ▼
[T8]  LoRa SPI 通路测试（共用 FSPI 总线，CS=GPIO8）
  │   RST 复位 → 等待 BUSY=LOW → GetStatus 命令 → MISO 非全 0xFF → 自动判断
  ▼
[T9]  ES8311 CODEC 测试
  │   T9-A: I2C 设备检测（自动）
  │   T9-B: PA_CTRL 使能 + I2S 播放 1kHz 正弦波 3s（人工听音确认）
  ▼
[T10] LMD4737 DMIC 测试
  │   Phase-1: 操作员发声 + RMS 检测（自动）
  │   Phase-2: 若无声则自动 Loopback（扬声器播放 → DMIC 录音）验证通路
  ▼
[T11] 测试汇总
      显示所有测试项 PASS/FAIL/SKIP 列表
      串口输出 FACTORY_TEST=OK 或 FACTORY_TEST=FAIL:<Tx,...>
      固件 halt
```

---

## 5. 各测试项详细说明

### T0 — 系统启动

- 初始化 `Serial`（115200）
- GPIO41（PA_CTRL）初始化为输出，默认 **LOW**（功放关断）
- 初始化 SPI0 并驱动 EPD（`GxEPD2::init()`），显示欢迎页：

**屏幕布局**：
```
┌─────────────────────────────┐
│  NM-Display-420             │
│  Factory Test FW v1.3       │
│                             │
│  Press AP to Start          │
└─────────────────────────────┘
```

- 串口输出：`[FACTORY TEST] Board: NM-Display-420`

---

#### 通用屏幕显示规范（适用于 T1~T9）

每个测试项进入时，屏幕固定格式如下：

```
┌─────────────────────────────┐
│      Tx  <Test Title>       │  <- 整行水平居中，Tx 大号红字，Title 正常黑字
│─────────────────────────────│
│       <Status / Hint>       │  <- 每行文字均水平居中
│                             │
│       [PASS] / [FAIL]       │  <- 结果行居中显示
│     AP=Next  BOOT=Fail      │  <- 提示行居中显示
└─────────────────────────────┘
```

> **居中规则**：所有文字（标题行、内容行、结果行、按键提示行）均使用 `getTextBounds()` 测量宽度后计算居中 X 坐标，不使用左对齐固定偏移。

---

### T1 — EPD 显示测试

**目的**：验证 E-Paper 屏幕三色（黑/白/红）及文字渲染正常。

**步骤**：依次做 4 次全屏刷新，每次刷新前先在缓冲区叠加状态标题和提示文字，随底色一起写入屏幕。

| Round | BG    | Text color | Screen content |
|-------|-------|------------|----------------|
| 1 | White | Black | `T1 EPD Display` / `Fill: WHITE` / `Check: pure white?` |
| 2 | Black | White | `T1 EPD Display` / `Fill: BLACK` / `Check: pure black?` |
| 3 | Red   | Black | `T1 EPD Display` / `Fill: RED`   / `Check: pure red?`   |
| 4 | White (restore) | Black | `T1 EPD Display` / `Text Render Test` / multi-size sample + corner marks |

**第 4 轮屏幕布局示例**：
```
┌─────────────────────────────────┐
│ T1                              │
│ EPD Display Test                │
│─────────────────────────────────│
│ Text Render                     │  <- large font
│ Hello World                     │  <- medium font
│ Small 0123456789                │  <- small font
│                             ●   │  <- top-right mark
│ ●                               │  <- bottom-left mark
│─────────────────────────────────│
│ 3-color OK? Text clear?         │
│ AP=PASS   BOOT=FAIL             │
└─────────────────────────────────┘
```

**判断**：人工查看三色是否鲜明、文字是否清晰。AP=PASS / BOOT=FAIL。

> 每轮全刷约 4~15s，T1 共 4 轮约 **60~90 秒**。每轮刷新开始前串口输出当前轮次日志，方便上位机监控进度。

---

### T2 — WS2812 RGB LED 测试

**目的**：验证板载 WS2812 RGB LED 三色通道均正常发光。

**驱动方式**：使用 `Adafruit_NeoPixel`（底层通过 ESP32 RMT 驱动单总线时序），数据引脚 GPIO47，1 颗像素。

**屏幕布局**：
```
┌─────────────────────────────┐
│ T2                          │
│ WS2812 RGB LED Test         │
│─────────────────────────────│
│ Lighting: RED               │
│ R OK  G ...  B ...  W ...   │
│─────────────────────────────│
│ Colors change correctly?    │
│ AP=PASS   BOOT=FAIL         │
└─────────────────────────────┘
```

**步骤**：
1. 屏幕显示 T2 标题页（一次全刷）
2. 依次点亮以下颜色，每色保持 **600ms**，屏幕局部刷新"正在点亮"行：
   - **Red**   `(255, 0, 0)`     → mark progress `R OK`
   - **Green** `(0, 255, 0)`     → mark progress `G OK`
   - **Blue**  `(0, 0, 255)`     → mark progress `B OK`
   - **White** `(255, 255, 255)` → mark progress `W OK`
   - **Off**   `(0, 0, 0)`
3. 全部完成后屏幕显示操作提示，等待按键

**判断**：人工确认三色均正常点亮。AP=PASS / BOOT=FAIL。

---

### T3 — 按键测试

**目的**：验证 AP 键（GPIO45）和 BOOT 键（GPIO0）均可正常触发。

**屏幕布局**：
```
┌─────────────────────────────┐
│ T3                          │
│ Button Test                 │
│─────────────────────────────│
│ Press AP key                │
│                             │
│ AP   [ WAIT ]               │
│ BOOT [ WAIT ]               │
└─────────────────────────────┘
```
检测到按键后对应行变为 `[ OK ]`，两键均检测到后底部显示 `[PASS]`。

**步骤**：
1. Screen shows T3 title, prompt "Press AP key", poll GPIO45, timeout 10s
2. Detected → AP row changes to `[ OK ]`, prompt "Press BOOT key", poll GPIO0, timeout 10s
3. Detected → BOOT row changes to `[ OK ]`, auto PASS
4. Any key timeout → row changes to `[ !! ]`, show `[FAIL]`, wait AP to continue

> T3 期间 AP 键的按下**仅用于按键检测本身**，不触发"进入下一项"逻辑。

---

### T4 — AHT20 传感器测试

**目的**：验证 I2C 总线和 AHT20 传感器正常工作。

**屏幕布局**：
```
┌─────────────────────────────┐
│ T4                          │
│ AHT20 Sensor Test           │
│─────────────────────────────│
│ Initializing...             │
│                             │
│ Temp: -- C                  │
│ Humi: -- %                  │
│─────────────────────────────│
│ Result: [PASS]              │
│ AP=Next                     │
└─────────────────────────────┘
```

**步骤**：
1. 拉高 GPIO40（电源使能），延时 200ms
2. `Wire.begin(39, 38)` 初始化 I2C，启动 AHT20（最多 5 次重试，间隔 40ms）
3. Read temperature + humidity, refresh screen with measured values
4. Auto-judge, show `[PASS]` or `[FAIL: reason]`, wait for AP key to continue

**自动判断条件**（全部满足 → PASS）：
- `aht.begin()` 返回 true
- 温度在 **-10°C ~ 60°C**
- 湿度在 **1% ~ 99%**

---

### T5 — 电池电压 ADC 测试（占位）

**当前状态**：⚠️ 当前版本板子尚未接入电池 ADC 电路，测试时自动标记 `SKIP`，不计入 PASS/FAIL 统计。

**屏幕显示**：
```
┌─────────────────────────────┐
│ T5                          │
│ Battery ADC                 │
│─────────────────────────────│
│ [SKIP] HW not populated     │
│ Skipping...                 │
└─────────────────────────────┘
```

**预留实现方案**（硬件就绪后启用）：
- 计算：`V_bat(mV) = analogRead(A0) × 3300 × 2 / 4095`
- 判断：原始值不为 0/4095，且电压在 **2800mV ~ 4350mV**
- 启用：将宏 `BATTERY_ADC_AVAILABLE` 改为 `1`

---

### T6 — WiFi 扫描测试

**目的**：验证 WiFi 模块可正常工作（无需连接，仅扫描）。

**屏幕布局**：
```
┌─────────────────────────────┐
│ T6                          │
│ WiFi Scan Test              │
│─────────────────────────────│
│ Scanning...                 │
│                             │
│ Found APs: 5                │
│ 1. SSID_A                   │
│ 2. SSID_B                   │
│ 3. SSID_C                   │
│─────────────────────────────│
│ Result: [PASS]              │
│ AP=Next                     │
└─────────────────────────────┘
```

**步骤**：
1. `WiFi.mode(WIFI_STA)` → `WiFi.scanNetworks()`（同步，超时 10s）
2. 屏幕刷新显示 AP 数量及前 3 个 SSID
3. 自动判断，等待 AP 键继续

**自动判断条件**：发现 AP 数量 ≥ 1 → PASS，否则 FAIL。

---

### T7 — SD 卡测试

**目的**：验证 TF/SD 卡 SPI 接口正常，卡可挂载并能读写。

**引脚**：CLK=9, MOSI(CMD)=10, MISO(DAT0)=11, CS=7（独立 SPI1，与 EPD SPI0 不冲突）

**屏幕布局**：
```
┌─────────────────────────────┐
│ T7                          │
│ SD Card R/W Test            │
│─────────────────────────────│
│ Init... OK                  │
│ Type: SDHC                  │
│ Size: 32 GB                 │
│ Write... OK                 │
│ Read back... OK             │
│─────────────────────────────│
│ Result: [PASS]              │
│ AP=Next                     │
└─────────────────────────────┘
```
Each step appends status (OK / FAIL) as it completes; no need to wait for all steps to finish before refreshing.

**步骤**：
1. 初始化独立 `SPIClass spi_sd(FSPI)`，`spi_sd.begin(9, 11, 10, 7)`
2. `SD.begin(7, spi_sd)` 挂载 SD 卡（超时 3s）
3. 读取卡信息（类型、容量），屏幕刷新显示
4. 写入测试文件 `/factory_test.txt`（内容："NM-Display-420 factory test OK"）
5. 回读文件内容，校验与写入一致
6. 删除测试文件，卸载，显示最终结果

**自动判断条件**（全部满足 → PASS）：
- `SD.begin()` 返回 true
- 卡类型不为 `CARD_NONE`
- 写入/回读内容一致

---

### T8 — LoRa SPI 通路测试

**目的**：验证 LoRa 模块焊盘 SPI 通路接通，芯片可响应命令（不涉及 RF 功能）。

**引脚**：
| 信号 | GPIO |
|------|------|
| NSS (CS) | 8 |
| MOSI | 10 |
| MISO | 11 |
| SCK | 9 |
| BUSY | 13 |
| RST | 12 |
| DIO1 | 14（本测试不使用）|

> MOSI/MISO/SCK 与 SD 卡共用 FSPI 总线，仅 CS 不同（SD=GPIO7，LoRa=GPIO8）。T7 已初始化的 `spi_sd` 实例可复用，或重新 `begin()` 同一组引脚。

**屏幕布局**：
```
┌─────────────────────────────┐
│ T8                          │
│ LoRa SPI Bus Test           │
│─────────────────────────────│
│ Reset chip... OK            │
│ Wait BUSY low... OK         │
│ GetStatus cmd...            │
│ MISO: 0x2C (valid)          │
│─────────────────────────────│
│ Result: [PASS]              │
│ AP=Next                     │
└─────────────────────────────┘
```

**步骤**：
1. 初始化 GPIO12（RST）为输出，GPIO13（BUSY）为输入上拉
2. 复位芯片：RST=LOW → 延时 2ms → RST=HIGH，屏幕显示 "Reset chip... OK"
3. 等待 BUSY=LOW（超时 1s），屏幕显示 "Wait BUSY low..."，超时 → FAIL
4. 使用 FSPI（GPIO9/10/11），CS=GPIO8，速率 1MHz，发送 3 字节 `[0xC0, 0x00, 0x00]`（GetStatus 命令，SX1262 兼容）
5. 读回 3 字节响应，取第 2 字节作为状态
6. 屏幕显示原始 MISO 值（`MISO: 0xXX`）
7. 自动判断

**自动判断条件**：
- BUSY 在超时前拉低 → SPI 时钟线/RST 线接通
- 响应字节不全为 `0xFF`（MISO 浮空/断线）且不全为 `0x00` → 数据线接通，芯片响应

> 此测试**不**验证 LoRa RF 功能，只验证 PCB 焊盘与 SPI 总线的电气连通性。DIO1（GPIO14）在本测试中不使用，仅作为 GPIO 输入悬空。

---

### T9 — ES8311 CODEC 测试

**目的**：验证 ES8311 I2C 通信正常、外部功放使能正常、I2S 音频播放正常。

**屏幕布局（T9-A）**：
```
┌─────────────────────────────┐
│ T9                          │
│ ES8311 CODEC                │
│─────────────────────────────│
│ I2C probe (0x18)... OK      │
│─────────────────────────────│
│ Result: [PASS]              │
│ AP=Next                     │
└─────────────────────────────┘
```

**屏幕布局（T9-B）**：
```
┌─────────────────────────────┐
│ T9                          │
│ ES8311 CODEC - Playback     │
│─────────────────────────────│
│ PA Amp enabled              │
│ Playing 1kHz tone...        │
│ Remaining: 3s               │
│─────────────────────────────│
│ Can you hear 1kHz tone?     │
│ AP=PASS   BOOT=FAIL         │
└─────────────────────────────┘
```

#### T8-A：I2C 设备检测（自动）

1. I2C 总线（SDA=39, SCL=38，T4 已初始化）
2. 对地址 `0x18`（`ES8311_CODEC_DEFAULT_ADDR`）发起写探测
3. ACK → screen shows `I2C probe (0x18)... OK`; NACK → `FAIL` (T9-B also skipped)

#### T8-B：I2S 播放测试（人工听音确认）

**实现方式**：使用 `esp_codec_dev` 框架，`esp_codec_dev_write()` 输出 PCM。

1. **Pull GPIO41 (PA_CTRL) HIGH** to enable external amplifier; screen shows "PA Amp enabled"
2. 配置 I2S（IDF `driver/i2s_std.h`，Master/Philips/16kHz/16bit/Stereo）：
   - MCLK=21, BCLK=15, LRCK=17, DOUT=16, DIN=18
3. 通过 `esp_codec_dev` 初始化 ES8311（`ESP_CODEC_DEV_WORK_MODE_BOTH`）
4. `esp_codec_dev_set_out_vol(output_dev, 75.0)` 设置输出音量
5. 生成 **1kHz 正弦波** PCM 缓冲（16kHz/16bit/1s），循环播放 **3 次**，屏幕显示倒计时
6. 播放完成后屏幕显示操作提示，等待按键
7. 判断后，**拉低 GPIO41**（PA_CTRL=LOW）关断功放

**判断**：T9-A 自动；T9-B 人工确认。AP=PASS / BOOT=FAIL。

> 板上扬声器为 **4Ω 喇叭** + 外部功放（PA_CTRL=GPIO41 使能），播放前务必先拉高 PA_CTRL。

---

### T9 — LMD4737 DMIC 测试

**目的**：验证数字麦克风 LMD4737 通过 ES8311 DMIC 接口录音正常，信号有效。

**架构说明**：  
LMD4737 为 PDM DMIC，时钟由 ES8311 `DMIC_CLK` 驱动，数据经 `DMIC_DAT` 回到 ES8311；ES8311 内部 PDM→PCM 转换后经 I2S ADC 通路输出到 ESP32 DIN（GPIO18）。录音使用 `esp_codec_dev_read(input_dev, buf, size)`。

**屏幕布局（Phase-1）**：
```
┌─────────────────────────────┐
│ T10                         │
│ LMD4737 DMIC Mic Test       │
│─────────────────────────────│
│ Make noise at the mic       │
│ (speak or clap)             │
│                             │
│ Countdown: 8s               │
│ RMS: ---                    │
│─────────────────────────────│
│ Recording...                │
└─────────────────────────────┘
```

**屏幕布局（Phase-2 Loopback）**：
```
┌─────────────────────────────┐
│ T10                         │
│ LMD4737 DMIC - Loopback     │
│─────────────────────────────│
│ No audio detected           │
│ Starting loopback test...   │
│ Play ref tone + record      │
│                             │
│ RMS: ---                    │
│─────────────────────────────│
│ Result: [PASS]              │
│ AP=Next                     │
└─────────────────────────────┘
```

**双阶段测试流程**：

#### Phase-1：操作员发声（主要路径）

1. 屏幕显示 T9 标题 + 倒计时提示，10s 内持续刷新 RMS 读数
2. `esp_codec_dev_set_in_gain(input_dev, 40.0)` 设置录音增益
3. 以 **16kHz/16bit/单声道** 录制 **2 秒**（≈64KB）到 SPIRAM 缓冲
4. 计算 RMS 能量，屏幕显示测量值
5. 若 **RMS > `DMIC_RMS_THRESHOLD_VOICE`（初始值：150）** → 显示 `[PASS]`，跳过 Phase-2

#### Phase-2：自动 Loopback（兜底路径）

> 操作员未发声（RMS ≤ 阈值）时自动触发，无需操作员介入。

1. 屏幕切换到 Loopback 布局
2. 拉高 GPIO41（PA_CTRL）使能功放
3. `esp_codec_dev_write()` 播放 **1kHz 正弦波 2 秒**（扬声器发声，DMIC 拾取）
4. 同步 `esp_codec_dev_read()` 录制 **2 秒**（TX/RX 双通道并行）
5. 计算录音缓冲 RMS，屏幕显示测量值
6. 若 **RMS > `DMIC_RMS_THRESHOLD_LOOPBACK`（初始值：50）** → `[PASS]`，否则 `[FAIL]`
7. 拉低 GPIO41 关断功放

**内存需求**：≈64KB，从 SPIRAM 分配（`heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`）。

---

### T11 — 测试汇总

**显示内容示例**：
```
======= 出厂测试结果 ========
T1  EPD Display      [PASS]
T2  RGB LED          [PASS]
T3  Buttons          [PASS]
T4  AHT20 Sensor     [PASS]
T5  Battery ADC      [SKIP]  <- HW not populated
T6  WiFi Scan        [PASS]
T7  SD Card          [PASS]
T8  LoRa SPI Bus     [PASS]
T9  ES8311 CODEC     [PASS]
T10 LMD4737 DMIC     [PASS] (Loopback)
===================================
   All PASS - Ready to ship
```

> `SKIP` 项不计入总体 PASS/FAIL 判断。

**串口输出**：
- 全部 PASS（SKIP 不计）→ `FACTORY_TEST=OK`
- 有 FAIL → `FACTORY_TEST=FAIL:T2,T9`

固件进入 **halt 循环**，等待断电或手动复位。

---

## 6. 项目结构规划

```
NM-Display-420/
├── platformio.ini
└── src/
    ├── main.cpp                  ← 入口，调用 TestRunner
    ├── config.h                  ← 所有引脚宏、阈值宏、Feature 开关
    ├── test_runner.h/.cpp        ← 状态机 + 按键导航 + 结果汇总
    ├── tests/
    │   ├── test_epd.h            ← T1  EPD 三色显示
    │   ├── test_led.h            ← T2  WS2812 RGB LED 三色测试
    │   ├── test_button.h         ← T3  双按键检测
    │   ├── test_aht20.h          ← T4  AHT20 温湿度
    │   ├── test_adc.h            ← T5  电池 ADC（占位）
    │   ├── test_wifi.h           ← T6  WiFi 扫描
    │   ├── test_sdcard.h         ← T7  SD 卡读写
    │   ├── test_lora.h           ← T8  LoRa SPI 通路测试
    │   ├── test_es8311.h         ← T9  ES8311 I2C + I2S 播放
    │   └── test_dmic.h           ← T10 LMD4737 DMIC 录音能量检测
    └── ui/
        └── display_helper.h      ← EPD 显示封装（基于 GxEPD2）
```

### config.h 关键宏预览

```cpp
// ── 引脚 ──────────────────────────────────────
#define PIN_EPD_SCK   2
#define PIN_EPD_MOSI  1
#define PIN_EPD_CS    3
#define PIN_EPD_DC    4
#define PIN_EPD_RST   5
#define PIN_EPD_BUSY  6

#define PIN_SD_CLK    9
#define PIN_SD_MOSI   10   // CMD
#define PIN_SD_MISO   11   // DAT0
#define PIN_SD_CS     7

// LoRa 与 SD 共用 FSPI 总线（GPIO9/10/11），独立 CS
#define PIN_LORA_NSS  8
#define PIN_LORA_RST  12
#define PIN_LORA_BUSY 13
#define PIN_LORA_DIO1 14
// MOSI/MISO/SCK 复用 PIN_SD_MOSI/MISO/CLK

#define PIN_I2S_MCLK  21
#define PIN_I2S_BCLK  15
#define PIN_I2S_LRCK  17
#define PIN_I2S_DOUT  16
#define PIN_I2S_DIN   18

#define PIN_PA_CTRL   41   // HIGH = 功放使能
#define PIN_I2C_SDA   39
#define PIN_I2C_SCL   38
#define PIN_TEMP_CTL  40   // AHT20 电源使能

#define PIN_BOOT_BTN  0
#define PIN_AP_BTN    45
#define PIN_LED       47   // WS2812 数据引脚（RMT/NeoPixel）
#define WS2812_COUNT  1    // 板载 WS2812 数量

// ── Feature 开关 ──────────────────────────────
#define BATTERY_ADC_AVAILABLE   0   // 0=SKIP, 1=测试

// ── DMIC 阈值 ─────────────────────────────────
#define DMIC_RMS_THRESHOLD_VOICE     150
#define DMIC_RMS_THRESHOLD_LOOPBACK  50
```

---

## 7. 依赖库

| 库 | 用途 | 引入方式 |
|----|------|---------|
| `GxEPD2 @ 1.6.8` | E-Paper 驱动 | PlatformIO lib_deps |
| `Adafruit AHTX0` | AHT20 温湿度传感器 | PlatformIO lib_deps |
| `Adafruit BusIO` | I2C/SPI 抽象层 | PlatformIO lib_deps |
| `Adafruit Unified Sensor` | 传感器事件统一接口 | PlatformIO lib_deps |
| `Adafruit NeoPixel` | WS2812 RGB LED 驱动（RMT）| PlatformIO lib_deps |
| Arduino `WiFi.h` | WiFi 扫描 | 内置 |
| Arduino `Wire.h` | I2C（AHT20 + ES8311 共用） | 内置 |
| Arduino `SD.h` + `SPI.h` | SD 卡读写 | 内置 |
| ESP-IDF `driver/i2s_std.h` | I2S 音频（IDF v5 API）| ESP-IDF 内置 |
| ESP-IDF `driver/i2c_master.h` | I2C master（ES8311 控制）| ESP-IDF 内置 |
| `espressif/esp_codec_dev` | ES8311 CODEC 驱动框架 | **拷贝到 `components/`**（见下） |

### esp_codec_dev 集成策略

PlatformIO Arduino 框架下，managed component 方式兼容性差，采用**组件直接拷贝**方式：

```
NM-Display-420/
└── components/
    └── esp_codec_dev/     ← 从 NM-Display-28inch/managed_components/ 复制
        └── ...
```

在 `platformio.ini` 中追加：
```ini
build_flags =
    -I components/esp_codec_dev/include
    -I components/esp_codec_dev/device/include
```

并将 `.cpp/.c` 文件通过 `build_src_filter` 或手动加入编译。

---

## 8. 引脚总表

| GPIO | 功能 | 方向 |
|------|------|------|
| 0 | BOOT 按键 | 输入（上拉） |
| 1 | EPD MOSI（SPI0） | 输出 |
| 2 | EPD SCK（SPI0） | 输出 |
| 3 | EPD CS | 输出 |
| 4 | EPD DC | 输出 |
| 5 | EPD RST | 输出 |
| 6 | EPD BUSY | 输入 |
| 7 | SD CS（SPI1） | 输出 |
| 8 | LoRa NSS/CS（SPI1，共用 FSPI） | 输出 |
| 9 | SD CLK / LoRa SCK（SPI1） | 输出 |
| 10 | SD MOSI/CMD / LoRa MOSI（SPI1） | 输出 |
| 11 | SD MISO/DAT0 / LoRa MISO（SPI1） | 输入 |
| 12 | LoRa RST | 输出 |
| 13 | LoRa BUSY | 输入（上拉）|
| 14 | LoRa DIO1 | 输入（本测试悬空）|
| 15 | I2S BCLK（ES8311） | 输出 |
| 16 | I2S DOUT → ES8311 DAC | 输出 |
| 17 | I2S LRCK（ES8311） | 输出 |
| 18 | I2S DIN ← ES8311 ADC | 输入 |
| 21 | I2S MCLK（ES8311） | 输出 |
| 38 | I2C SCL（AHT20 + ES8311） | 双向 |
| 39 | I2C SDA（AHT20 + ES8311） | 双向 |
| 40 | AHT20 电源使能 | 输出（HIGH=开） |
| 41 | 功放使能（PA_CTRL） | 输出（HIGH=使能） |
| 45 | AP 按键 | 输入（上拉） |
| 47 | WS2812 数据引脚（RGB LED） | 输出（RMT 单总线）|
| A0 | 电池电压 ADC（未接，占位） | 模拟输入 |

---

## 9. 已确认问题汇总

| 问题 | 确认结果 |
|------|---------|
| 框架 | PlatformIO + Arduino |
| T5 电池 ADC | 当前板未接，占位 SKIP，`BATTERY_ADC_AVAILABLE=0` |
| T6 WiFi 环境 | 测试现场有 WiFi 信号，自动判断 |
| T8 扬声器 | 4Ω 喇叭 + 外部功放，PA_CTRL=GPIO41 使能 |
| T10 DMIC 触发 | 双阶段：操作员发声优先，无声自动 Loopback |
| ES8311 初始化 | 复用 `NM-Display-28inch/esp_es8311_port.cpp`，修改引脚宏 |
| `esp_codec_dev` 集成 | 直接拷贝到 `components/` 目录 |
| T10 RMS 阈值 | 初始值 Phase-1:150 / Phase-2:50，首块板实测后校准 |

> ✅ 所有问题已确认，可开始实施。

---

## 10. 后续实施步骤

- [ ] 创建 PlatformIO 工程，配置 `platformio.ini`
- [ ] 拷贝 `esp_codec_dev` 组件，配置编译路径
- [ ] 编写 `config.h`（所有引脚宏 + 阈值宏）
- [ ] 实现 `display_helper.h`（EPD 封装）
- [ ] 实现 `test_runner`（状态机 + 按键导航）
- [ ] 实现 T1~T6 测试用例（无音频依赖）
- [ ] 实现 T7 SD 卡测试
- [ ] 实现 T8 LoRa SPI 通路测试（复用 FSPI，RST+BUSY+GetStatus）
- [ ] 实现 T9 ES8311 I2C 检测 + I2S 播放
- [ ] 实现 T10 LMD4737 DMIC 双阶段测试
- [ ] 实现 T11 汇总与串口输出
- [ ] 集成联调 + 首块板实测（校准 DMIC 阈值）
