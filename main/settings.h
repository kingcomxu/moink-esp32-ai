#ifndef MOINK_SETTINGS_H
#define MOINK_SETTINGS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

/*
 * 持久化设置（NVS 命名空间 "moink"）。页面所有画面参数（风格 / 强度 / 文字 /
 * 旋转…）都留在手机 localStorage，固件只存会影响「传输与电源」的这几项。
 *
 * ★ R1.2.2：升级**不再强制重置设置** —— 只清理本版已废弃的键
 * （settings_reset_for_upgrade()），屏型 / 功率 / GPIO 接法 / 休眠 / 唤醒 /
 * 热点凭据全部原样保留。读取时仍不做「历史值归一」，越界值一律回落默认档。
 *
 * ★ R1.2.2：引脚（SPI 六线 + 电池 ADC + 唤醒键）改为运行时可配，随 NVS 持久化；
 *   另有固定自锁脚（MOS 一键开机），不对外开放 —— ESP32 版为 GPIO13
 *   （C3 版的 GPIO2 在 ESP32 上是 strapping 脚，不能用）。
 */

#define SETT_SSID_MAX 33
#define SETT_PASS_MAX 65

/* 休眠时长档位（秒）。0 = 不休眠。 */
#define SETT_SLEEP_OFF    0
#define SETT_SLEEP_1MIN   60
#define SETT_SLEEP_3MIN   180
#define SETT_SLEEP_5MIN   300

/* 定时自动唤醒档位（秒）。0 = 关闭。 */
#define SETT_WAKE_OFF     0
#define SETT_WAKE_1H      3600
#define SETT_WAKE_12H     43200
#define SETT_WAKE_1D      86400

/* 超时动作（R1.3.3）：空闲达到「超时」时长后执行什么。 */
#define SETT_ACTION_SLEEP 0   /* 进入深睡（可配自动唤醒） */
#define SETT_ACTION_OFF   1   /* 释放自锁断电（需硬件自锁电路，见 PIN_SELF_LOCK） */

/* WiFi 发射功率档位。高 = 18dBm；中 = 10dBm；低 = 8.5dBm（R1.3.3 起为默认档）。 */
#define SETT_WIFI_PWR_HIGH  0
#define SETT_WIFI_PWR_MID   1
#define SETT_WIFI_PWR_LOW   2

/* ---------------- 引脚配置（ESP32 经典款移植版） ----------------
 *
 * ESP32（esp32 / WROOM-32 / DevKitC）的硬边界（Espressif 官方 GPIO 表）：
 *   不可用         GPIO6..11 接片内 SPI flash；GPIO1/3 为 UART0（本项目作控制台）。
 *   Strapping 脚   GPIO0 / GPIO2 / GPIO12 / GPIO15 在复位瞬间被采样决定启动模式
 *                  —— 一律不开放给用户，否则「上电被拉高」就起不来。
 *   仅输入         GPIO34..39 无输出驱动、也没有内部上下拉（外部需自行上拉）。
 *   电池 ADC       只能 ADC1（ADC2 与 WiFi 冲突）：GPIO36/37/38/39/32/33/34/35
 *                  = ADC1_CH0..CH7（★ 顺序不按引脚号排，见 power.c 映射表）。
 *   深睡唤醒       只有 RTC GPIO 支持深睡唤醒；本项目用 ext0（单脚电平）+ 内部
 *                  上拉，因此唤醒脚限定「有内部上拉的非 strapping RTC GPIO」：
 *                  GPIO4 / 14 / 25 / 26 / 27 / 32 / 33。
 *
 * 可用输出脚白名单：4,5,14,16,17,18,19,21,22,23,25,26,27,32,33
 * （BUSY 额外可用仅输入脚 34..39；自锁脚 GPIO13 单独排除在白名单外）
 */

/* 8 个功能的引脚索引；该顺序同时也是页面「自定义」菜单的排列顺序。 */
enum {
    PIN_IDX_ADC  = 0,   /* 电池 ADC   ：仅 GPIO32..39（ADC1） */
    PIN_IDX_WAKE,       /* 深睡唤醒键 ：仅 RTC GPIO 4/14/25/26/27/32/33 */
    PIN_IDX_SCK,
    PIN_IDX_MOSI,
    PIN_IDX_CS,
    PIN_IDX_DC,
    PIN_IDX_RST,
    PIN_IDX_BUSY,
    PIN_IDX_COUNT
};

/*
 * MOS 自锁一键开机脚：固件固定 GPIO13，上电第一件事就拉高并保持，
 * 深睡前锁存电平防止掉电。不对外开放配置（自定义菜单里也会排除它）。
 *
 * ★ ESP32 移植说明：C3 版用的是 GPIO2 —— 那是 ESP32 的 strapping 脚，
 *   上电被拉高会直接起不来，所以 ESP32 换到 GPIO13（RTC GPIO、可输出、
 *   非 strapping，支持 gpio_hold 深睡电平锁存）。
 */
#define PIN_SELF_LOCK  13

/*
 * 板载状态灯（蓝色 LED）：DevKitC / DevKit V1 这类开发板的板载蓝灯接 GPIO2。
 *
 * ★ 与自锁脚的区别：自锁脚是「上电即拉高」，会被 strapping 采样到，所以必须有
 *   外部上拉/电平顾虑；状态灯只在**启动完成之后**才被驱动输出，复位瞬间引脚仍是
 *   高阻（内部下拉），strapping 采样结果与不接灯时完全一致，因此可以留在 GPIO2。
 *   （GPIO2 仍在 settings.c 的用户可配白名单之外，页面选不到它。）
 *
 * 换板 / 板上无灯时改这里：脚位改数字；极性按接法改（高电平点亮 = 1，
 * 灌电流接法 = 0）；确实没有灯就设 -1，led 模块整体编译为空操作。
 */
#define PIN_STATUS_LED        2
#define PIN_STATUS_LED_ACTIVE 1

/* GPIO 预设（与页面下拉一一对应；取值即 gpio_preset）。 */
#define GPIO_PRESET_DEFAULT  0   /* 默认：现役接法 */
#define GPIO_PRESET_LINE     1   /* 排线 */
#define GPIO_PRESET_BOARD    2   /* 整合板 */
#define GPIO_PRESET_CUSTOM   3   /* 自定义 */
#define GPIO_PRESET_COUNT    4
#define GPIO_PRESET_NPRESET  3   /* 前 3 档为固定预设，第 4 档为用户自填 */

typedef struct { uint8_t v[PIN_IDX_COUNT]; } pin_cfg_t;

/* 固定预设表：[0] 默认 / [1] 排线 / [2] 整合板。 */
extern const pin_cfg_t PIN_PRESETS[GPIO_PRESET_NPRESET];

/* 可作**输出**的数字 IO（SCK/MOSI/CS/DC/RST）：白名单且不等于自锁脚。 */
bool pin_io_allowed(uint8_t io);
/* 可作**输入**的数字 IO（BUSY）：上表 + 仅输入脚 GPIO34..39。 */
bool pin_in_allowed(uint8_t io);
/* ADC 脚合法集合：GPIO32..39（ADC1）。 */
bool pin_adc_allowed(uint8_t io);
/* 唤醒脚合法集合：GPIO4/14/25/26/27/32/33（带内部上拉的非 strapping RTC GPIO）。 */
bool pin_wake_allowed(uint8_t io);
/* 整表校验：每脚在各自合法域内、彼此不重复、避开自锁脚。合法返回 true。 */
bool pin_cfg_valid(const pin_cfg_t *c);
/* 取当前生效引脚表（永不为 NULL）。 */
const pin_cfg_t *settings_pins(void);

typedef struct {
    uint8_t  panel;      /* EPD_PANEL_A0 / EPD_PANEL_A1 */
    uint8_t  hflip;      /* 保留字段：800x600 对照档已删除，当前无生效档位 */
    uint8_t  wifi_pwr;   /* SETT_WIFI_PWR_HIGH / MID / LOW */
    uint8_t  gpio_preset;/* GPIO_PRESET_DEFAULT / LINE / BOARD / CUSTOM */
    pin_cfg_t pins;      /* 生效引脚表（CUSTOM 时即用户所填，否则等于对应预设） */
    uint32_t sleep_s;    /* 空闲超时时长；0 = 不超时（既不睡也不关） */
    uint8_t  timeout_action; /* SETT_ACTION_SLEEP / SETT_ACTION_OFF（R1.3.3） */
    uint32_t wake_s;     /* 定时自动唤醒间隔；0 = 关闭 */
    uint8_t  wake_show;  /* 定时唤醒后自动切到下一张；1 = 是（默认，R1.3.3） */
    char     ap_ssid[SETT_SSID_MAX];  /* 空 = 用默认 MoInk-XXXX */
    char     ap_pass[SETT_PASS_MAX];  /* 空 = 热点开放 */
} moink_settings_t;

void settings_init(void);
const moink_settings_t *settings_get(void);

/* 各 setter 立即写 NVS；失败仅告警不影响 RAM 态。 */
esp_err_t settings_set_panel(uint8_t v);
esp_err_t settings_set_hflip(uint8_t v);
esp_err_t settings_set_wifi_pwr(uint8_t v);
esp_err_t settings_set_sleep(uint32_t v);
esp_err_t settings_set_wake(uint32_t v);
/* R1.3.3：空闲超时后的动作（休眠 / 关机断电）。 */
esp_err_t settings_set_timeout_action(uint8_t v);
/* R1.3.3：定时唤醒后是否自动切图（默认 1）。 */
esp_err_t settings_set_wake_show(uint8_t v);
/*
 * 设置引脚。preset 取 GPIO_PRESET_*：
 *   0/1/2 从固定预设表取（custom 被忽略）；
 *   3     使用 custom，必须通过 pin_cfg_valid()，否则返回 ESP_ERR_INVALID_ARG。
 * 引脚改动需重启才生效（SPI 总线在启动时按引脚初始化），调用方负责提示。
 */
esp_err_t settings_set_gpio(uint8_t preset, const pin_cfg_t *custom);
/* FB-014①：ssid / pass 任一为 NULL = 保持当前值；pass 为空串 = 清除密码（热点开放）。 */
esp_err_t settings_set_ap(const char *ssid, const char *pass);

/* 恢复出厂：擦除整个 NVS 命名空间（含热点凭据与页面热更标记）；调用方负责重启。 */
void settings_factory_reset(void);

/*
 * 固件升级收尾：**只清理已废弃的键**，不再重置用户设置（R1.2.2）。
 *   - 清理：a1_mode（A1 档位已删除）、a11_var（A1.1 诊断变体已删除）等旧版遗留键。
 *   - 保留：panel / hflip / wifi_pwr / gpio_preset + 8 个引脚键 / sleep_s / wake_s /
 *     ap_ssid / ap_pass —— 它们描述硬件与用户偏好，重置会把人挡在门外。
 *   - 页面热更标记（web_len / web_crc / page_ver）由 ota_web_clear() 负责清除。
 * 调用方在固件校验通过、确定要切槽重启之后调用。
 */
void settings_reset_for_upgrade(void);

#endif /* MOINK_SETTINGS_H */
