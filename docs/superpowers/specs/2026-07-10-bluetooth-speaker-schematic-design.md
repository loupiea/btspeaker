# ESP32 2.0 蓝牙音箱原理图设计规格

日期：2026-07-10

## 1. 目标与范围

设计一块完整自研的 2.0 声道蓝牙音箱主板，不使用现成蓝牙、功放、电源或 USB Host 模块。首版原理图覆盖：

- ESP32 Classic Bluetooth A2DP 音频接收；
- TF 卡和 U 盘 MP3/WAV 本地播放；
- 3.5 mm AUX 立体声输入；
- 双路 MAX98357A I2S 数字功放；
- OLED、旋转编码器、播放/暂停键和音源切换键；
- USB-C 5 V 输入、1S 锂电池充放电管理和边充边播验证；
- UART 下载、关键总线和电源测试点。

首版不实现 USB Audio、耳机输出、麦克风、Wi-Fi 音频、PSRAM 或独立上一首/下一首按键。

## 2. 系统架构

主控采用 ESP32-D0WD-V3 裸片，外接 16 MB SPI Flash、40 MHz 晶振和 PCB 倒 F 天线。蓝牙、TF 卡、U 盘和 AUX 音源统一在 ESP32 固件中完成选择、解码和数字音量处理，再通过 I2S0 输出到左右两颗 MAX98357A。

TF 卡和 CH376S 共用 VSPI，仅使用独立片选区分。AUX 经 ES8388 ADC 转为 I2S1 输入。OLED 与 ES8388 共用 I2C。USB-C 只承担 5 V 输入和充电；USB-A 只承担 U 盘 Host。

## 3. 主控、Flash 与射频

- U1：ESP32-D0WD-V3，QFN48。
- U2：W25Q128JV，16 MB、3.3 V SPI Flash，连接 ESP32 专用 GPIO6 至 GPIO11。
- Y1：40 MHz、精度不差于 ±10 ppm；负载电容依据所选晶振 CL 和板级杂散电容计算，保留 XTAL_P 串联 0 Ω调试位。
- EN：10 kΩ上拉、1 µF 对地和复位按键；最终 RC 值在 ERC 后按乐鑫上电时序复核。
- GPIO0：10 kΩ上拉并接 BOOT 按键，不复用普通功能。
- UART0：GPIO1/TXD、GPIO3/RXD，引出 3.3 V、GND、TXD、RXD、EN、GPIO0 六针下载接口。
- RF：芯片端使用 CLC 匹配网络并靠近 RF 引脚，器件封装 0201；板边 PCB 倒 F 天线前再预留 π 型调谐网络和射频测试焊盘。
- RF 走线按 50 Ω单端阻抗设计，不能跨层、分叉或靠近晶振、Flash、USB、功放输出和开关电源。

RF 元件初始范围采用乐鑫硬件指南建议：芯片端匹配电容约 1.2 至 1.8 pF、电感约 2.0 至 3.0 nH；最终值必须通过整板射频测试确定，不能直接视为量产值。

## 4. 电源与电池

### 4.1 USB-C 与 IP5306

- J1：USB-C 16P 母座，仅使用 VBUS、CC1、CC2、GND 和 Shield。
- CC1、CC2 各接 5.1 kΩ到 GND，配置为 5 V Sink。
- VBUS 加 USB 口 ESD/TVS、2 A 级自恢复保险丝和输入电容。
- U3：IP5306，ESOP8；按所采购版本的数据手册典型应用连接 VIN、BAT、SW、VOUT、KEY 和 LED。
- L1：1 µH，饱和电流和温升满足 2.4 A Boost 路径。
- 电池：1S 3.7 V、2500 mAh、持续放电能力至少 3 A、带保护板，通过防呆 2P 接口连接。
- KEY 接独立实体电源键；LED 状态脚接电量指示灯并保留测试点。
- IP5306 输出定义为 SYS_5V，给功放、USB-A 负载开关和 3.3 V Buck 供电。

IP5306 的边充边播能力作为首版重点实测项。若满负载时出现 5 V 跌落、重启或温升超限，第二版再引入更强的负载共享/电源路径方案。

### 4.2 3.3 V Buck

- U4：SY8088IAAC，SOT23-5，输入 SYS_5V，输出 +3V3。
- L2：2.2 µH；CIN、COUT 各 10 µF，并按数据手册缩短开关电流环路。
- 反馈初值：上臂 100 kΩ、下臂 22.1 kΩ，目标约 3.3 V；保留 Cff 调试焊盘。
- +3V3 给 ESP32、Flash、ES8388 数字侧、CH376S 逻辑侧、TF 卡、OLED 和控制电路供电。
- 每个 IC 供电脚就近放置 100 nF，按芯片手册增加 1 µF/10 µF 储能电容。

## 5. 音频链路

### 5.1 I2S0 与双路功放

I2S0 引脚固定为：

| 信号 | ESP32 GPIO |
| --- | ---: |
| AMP_BCLK | GPIO26 |
| AMP_LRCLK | GPIO25 |
| AMP_DOUT | GPIO27 |
| AMP_SD_MUTE | GPIO16 |

- U5/U6：MAX98357A，TQFN16，SYS_5V 供电。
- 两颗芯片共用 BCLK、LRCLK 和 DIN；每根线上预留 22 Ω至 33 Ω源端串联阻尼焊盘。
- 左声道 SD_MODE 使用 3.3 V 逻辑高配置；右声道按 MAX98357A 数据手册使用约 210 kΩ的 3.3 V 拉升路径选择右声道。
- 两路 SD_MODE 均可由 AMP_SD_MUTE 拉低关断；实现时使用独立开漏器件，不能破坏左右声道电平窗口。
- GAIN_SLOT 默认接 GND，设置 12 dB；保留改值焊盘，若实测削顶或底噪偏高则降至 9 dB/6 dB。
- 每颗功放 VDD 就近放置 100 nF 和 10 µF。
- 输出分别接 L+/L-、R+/R- 端子，驱动 4 Ω扬声器；BTL 负端不得接系统 GND。

### 5.2 AUX 与 ES8388

- J4：3.5 mm 立体声 AUX 输入，外壳接地方式明确，并加低电容 ESD。
- 左右声道经 AC 耦合、限流和低通网络接 ES8388 Line-In 推荐输入。
- U7：ES8388，QFN28；模拟电源由 +3V3 经磁珠和就近电容滤波，数字电源接 +3V3。
- ESP32 为 I2S1 Master，ES8388 为 Slave。

| 信号 | ESP32 GPIO |
| --- | ---: |
| I2C_SDA | GPIO21 |
| I2C_SCL | GPIO22 |
| AUX_MCLK | GPIO33 |
| AUX_BCLK | GPIO32 |
| AUX_LRCLK | GPIO17 |
| AUX_DIN | GPIO35 |

ES8388 的模拟地不做割裂地岛；2 层板保持连续地平面，通过布局分区和短回流路径降低噪声。

## 6. 本地存储

### 6.1 共用 VSPI

| 信号 | ESP32 GPIO |
| --- | ---: |
| VSPI_SCLK | GPIO18 |
| VSPI_MISO | GPIO19 |
| VSPI_MOSI | GPIO23 |
| TF_CS | GPIO5 |
| CH376_CS | GPIO15 |

GPIO5 和 GPIO15 均加 10 kΩ上拉，保证复位阶段外设未选中，并在整机启动测试中复核绑带电平。两条 CS 不允许同时有效。

### 6.2 TF 卡

- TF 卡使用 SPI 模式和 3.3 V供电，接口附近放置 100 nF 与 10 µF。
- 卡座外壳接 GND；时钟和数据预留串联阻尼焊盘。
- Card Detect 首版只引出测试点，固件使用挂载/轮询判断。

### 6.3 CH376S 与 USB-A Host

- U8：CH376S 裸片，按 WCH 手册的 SPI、时钟、复位和 USB Host 典型应用连接。
- CH376S INT 首版只引出测试点，固件使用轮询。
- USB-A D+/D- 直接连接 CH376S USB Host 端，加入低电容 ESD，走线短且成对。
- USB-A VBUS 由 SYS_5V 经 TPS2553 类可调限流负载开关供电，默认限流约 500 mA；OC 引出测试点。
- USB-A 数据线不得与 USB-C、UART 或其他 USB 网络相连。

## 7. 人机交互与检测

- OLED：0.96 英寸、3.3 V I2C 接口，与 ES8388 共用 GPIO21/22；SDA/SCL 各由 4.7 kΩ上拉到 3.3 V。
- 音量编码器 A/B：GPIO34/GPIO39，外接 10 kΩ上拉，并预留 RC 消抖电容。
- 播放/暂停键：GPIO13，按下接 GND，10 kΩ上拉。
- 音源切换键：GPIO14，按下接 GND，10 kΩ上拉。
- 电源键：仅连接 IP5306 KEY，不连接 ESP32。
- 电池检测：GPIO36 经高阻分压和 100 nF 滤波采样 BAT+；分压后的最大电压低于 ADC 允许范围。

## 8. 原理图图页

1. USB-C、电池、IP5306 与 SYS_5V；
2. SY8088IAAC 与 +3V3 电源树；
3. ESP32、Flash、晶振、RF 与下载接口；
4. 双 MAX98357A、I2S0 与扬声器接口；
5. AUX、ES8388 与 I2S1；
6. TF 卡、CH376S、USB-A 与 VSPI；
7. OLED、编码器、按键、电池 ADC 和测试点。

图页之间只使用明确的全局电源网络和命名网络标签。网络命名遵循本规格中的大写名称，避免同一信号出现多个别名。

## 9. 2 层 PCB 约束

- 所有主要器件放在顶层，底层不放器件并尽量保持连续 GND。
- 裸片 ESP32、Flash、晶振和 RF 网络集中在板边安静区域；倒 F 天线按参考设计留出上下层禁布和禁铜区。
- IP5306、SY8088IAAC、CH376S USB 路径、MAX98357A 输出和 ES8388 模拟输入分区布置。
- RF 与晶振下方必须有完整参考地；天线禁布区除外。
- 功放 BTL 输出、USB-A VBUS 和电池大电流路径加宽，不穿越 AUX、RF、晶振或 Flash 区域。
- 允许扩大板尺寸以换取地连续性、测试空间和返修空间，不以最小尺寸为目标。

## 10. 检查与验收

原理图完成后必须：

- 运行 ERC，解决未连接电源脚、输入悬空、电源冲突和网络名错误；
- 人工复核 ESP32 全部电源脚、绑带脚、Flash 专用总线和下载接口；
- 对照各芯片数据手册逐脚检查 IP5306、SY8088IAAC、ES8388、CH376S 和 MAX98357A；
- 检查 BTL 扬声器负端未接地、USB-A 与 USB-C 数据完全隔离、两路 SPI CS 默认高；
- 检查每个 IC 的去耦、各电源测试点和关键数字总线测试点齐全；
- 输出 BOM 草案并标记 RF 匹配、晶振负载、Buck 反馈和音频滤波等调试值。

首次上电按电源输入/IP5306、3.3 V Buck、ESP32 下载启动、OLED、I2S 功放、TF、U 盘、AUX、蓝牙音源切换的顺序分阶段验证。

## 11. 设计依据

- Espressif ESP32 Hardware Design Guidelines；
- Espressif ESP32 Series Datasheet；
- Analog Devices MAX98357A/MAX98357B Datasheet Rev. 16；
- Injoinic IP5306 Datasheet V1.31；
- Silergy SY8088IAAC Datasheet；
- WCH CH376 Datasheet；
- Everest Semiconductor ES8388 Datasheet。
