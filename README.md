# 墨印 · MoInk — ESP32 经典款移植版（R1.4.0）

> 把喜欢的瞬间，放在身边。基于 **经典 ESP32（WROOM-32 / DevKitC）** 与华为拆机四色墨水屏（768×552 黑/白/黄/红）的电子拍立得。

这是 [atunverse/moink](https://github.com/atunverse/moink) R1.3.4（ESP32-C3 SUPERMINI 版）的 **ESP32 经典款移植分支**，改用 **纯 ESP-IDF + CMake**（`idf.py build`）构建。

功能与原版完全一致：手机连设备热点 → 浏览器里选图/裁剪/风格化/排版 → 一键刷屏；固件只做「热点 / 接收 / 刷图 / 休眠 / 唤醒 / 升级」。

---

## 与 C3 版的差异（移植要点）

| 项 | C3 版（R1.3.4） | ESP32 版（R1.4.0） |
|---|---|---|
| 构建 | PlatformIO + ESP-IDF | 纯 ESP-IDF + CMake（`idf.py`） |
| 可用 GPIO | 只有 0..10 | 0,2,4,5,12–19,21–23,25–27,32–39（flash 6..11 / UART0 1,3 / strapping 0,2,12,15 不开放） |
| 自锁脚 | GPIO2 | **GPIO13**（GPIO2 在 ESP32 上是 strapping 脚，上电拉高起不来） |
| 深睡唤醒 | `esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown()` | **`esp_sleep_enable_ext0_wakeup()`** + `rtc_gpio_*` + `RTC_PERIPH` 保持上电 |
| 电池 ADC | 通道号 = GPIO 号（GPIO0..4 = CH0..4） | **映射表**：36/37/38/39/32/33/34/35 = ADC1_CH0..CH7 |
| 帧缓冲 | 120000 B（800×600 遗留上限） | 105984 B（按实际 768×552 收窄，省 14 KB 堆） |
| OTA 芯片校验 | 写死 `chip_id == 0x0005`(C3) | 读 `CONFIG_IDF_FIRMWARE_CHIP_ID`（随 target 自动变） |
| 控制台 | USB-Serial-JTAG | UART0（GPIO1/3）@115200 |

**未改动**：帧格式契约（api=2）、分区表、NVS 布局、HTTP 路由、轮播存储、控制页交互逻辑 —— 两版分区表一模一样，只要编译目标对得上就能互相 OTA。

---

## 硬件清单与接线（默认档）

| 部件 | 规格 |
|---|---|
| 主控 | ESP32-WROOM-32 / ESP32-DevKitC（4 MB flash，无 PSRAM 也能跑） |
| 屏幕 | 华为拆机 768×552 四色电子墨水屏（黑/白/黄/红，2bpp） |
| 唤醒按键 | GPIO32 → GND（低电平；单击唤醒、双击切图、长按 20 s 恢复出厂） |
| 电池检测 | GPIO33（ADC1_CH5，1M:1M 分压，ADC 端建议并联 100 nF 到地） |
| 自锁（可选） | GPIO13 → MOS 一键开机电路（高电平维持供电；超时动作选「关机」时拉低断电） |

屏幕接线（默认档，SPI 走 VSPI 那组脚，实际由 GPIO 矩阵任意映射）：

| 屏 | SCK | MOSI(DIN) | CS | DC | RST | BUSY |
|---|---|---|---|---|---|---|
| ESP32 | 18 | 19 | 5 | 17 | 16 | 4 |

> 屏的 VCC 接 3.3 V、GND 共地。BUSY 由屏驱动，无需上下拉；若换成 GPIO34..39 这类仅输入脚，需要外部上拉。

另外两档预设（设置页 → GPIO 设置 → 接法）：

| 档位 | ADC | 唤醒 | SCK | MOSI | CS | DC | RST | BUSY |
|---|---|---|---|---|---|---|---|---|
| 0 默认 | 33 | 32 | 18 | 19 | 5 | 17 | 16 | 4 |
| 1 排线 | 35 | 26 | 14 | 21 | 22 | 25 | 27 | 33 |
| 2 整合板 | 32 | 4 | 18 | 23 | 5 | 22 | 21 | 16 |

自定义档的合法集合由固件校验（见 `main/settings.c`）：
- 电池 ADC：`32..39`（只有 ADC1 可用，ADC2 与 WiFi 冲突）
- 唤醒键：`4 / 14 / 25 / 26 / 27 / 32 / 33`（RTC 域 + 有内部上拉 + 非 strapping）
- SCK/MOSI/CS/DC/RST：`4,5,14,16,17,18,19,21,22,23,25,26,27,32,33`
- BUSY：上表 + 仅输入脚 `34..39`

---

## 编译与烧录

需要 ESP-IDF v5.2+（原项目用 6.1，本移植按 6.x 写，5.x 亦可）：

```bash
# 0. 进入 IDF 环境（Windows 用 ESP-IDF PowerShell / 命令提示符）

# 1. 设定目标芯片（只需做一次，生成 sdkconfig + build/）
idf.py set-target esp32

# 2. 编译（会自动把 page/index.html 固化进 main/index_html.h）
idf.py build

# 3. 烧录 + 看日志（COMx 换成实际串口）
idf.py -p COMx flash monitor
```

> 换过目标芯片后若报 `sdkconfig` 冲突，删掉 `sdkconfig` 重新 `set-target` 即可。
> 改了 `page/index.html` 不必手动跑脚本：`main/CMakeLists.txt` 挂了自动固化步骤，
> 页面或 `version.h` 比 `index_html.h` 新时会先重新生成再编译；
> 想手动跑：`python tools/make_index_header.py`（脚本内含「页面 meta 版本 == `MOINK_VERSION`」断言，不一致直接失败）。

---

## 使用

1. 上电后设备创建热点 `MoInk-XXXX`（XXXX 为 MAC 后 2 字节），手机连接后自动弹出创作页（或浏览器访问 `http://192.168.4.1`）。
2. 拍立得模式：选图 → 裁剪调整 → 风格/留白/文字 → 「上传并刷新屏幕」。
3. 便签模式：直接输入文字（描边 / 底色 / 四色字体）→ 上传。
4. 设置页（右上角 ⚙）：热点改名改密、电源管理（超时后休眠或关机）、残影清理、GPIO 接法、恢复出厂。
5. 升级：设置页「上传并升级」一个入口 —— 传 `.bin` = 固件升级（含控制页、保留热点名与密码、自动重启），传 `.html` = 只热更控制页。

按键语义：单击 = 唤醒（重置空闲计时）；双击 = 切到下一张轮播图；长按 20 s = 恢复出厂并重启。

---

## 目录结构

```
moink-esp32/
├── main/                 # 固件源码（ESP-IDF 组件）
│   ├── main.c            #   主入口：初始化 / 路由 / 任务 / 按键 / 睡眠与唤醒
│   ├── epd_drv.c         #   墨水屏驱动（A0 隔行映射 / A1 顺序直写）
│   ├── frame.c           #   帧协议接收与校验（CRC16）
│   ├── ota_web.c         #   HTTP 升级入口（内容嗅探分流）/ 控制页热更
│   ├── netif_ap.c        #   纯 SoftAP 网络接口
│   ├── captive.c         #   captive portal DNS + Apple 探测引导页
│   ├── settings.c        #   NVS 设置 + 引脚预设与校验（ESP32 版）
│   ├── power.c           #   电源管理 / 电池 ADC 映射 / ext0 深睡唤醒
│   ├── store.c           #   轮播帧存储（8 槽 x 128 KB）
│   ├── index_html.h      #   由 page/index.html 生成（勿手改）
│   └── CMakeLists.txt
├── page/index.html       # 创作页面（单文件，可独立热更）
├── tools/make_index_header.py   # 页面 → C 头文件
├── partitions.csv        # 分区表（nvs/ota_0/ota_1/web/store）
├── sdkconfig.defaults    # ESP32 默认配置
└── CMakeLists.txt
```

---

## 注意

- 色码固定：`0x00=黑 0x01=白 0x02=黄 0x03=红`（实测标定，改动前必读原项目 `docs/CONTRACT.md`）。
- 固件不做任何图像处理 —— 像素全部由页面在浏览器端合成后才发送。
- 深睡期间 `RTC_PERIPH` 电源域保持上电（ext0 唤醒依赖内部上拉），静态电流会略高于「全掉电」方案。
- 电池电压读数是粗测：`BAT_FULLSCALE_MV`(3100) 与分压比按板实测标定，换板请复核 `main/power.c`。
- 不要在固件里加蓝牙：会吃光堆并拖垮 SoftAP。
