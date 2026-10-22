#include "power.h"
#include "epd_drv.h"
#include "settings.h"
#include "led.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power";

/*
 * ESP32 的 ADC1 通道与引脚是**固定绑定且顺序不按引脚号排列**的：
 *   GPIO36/37/38/39/32/33/34/35 = ADC1_CH0..CH7
 * （C3 版「通道号 = GPIO 号」的写法在 ESP32 上完全错位，必须走这张表。）
 * 只能用 ADC1：ADC2 与 WiFi 共用，SoftAP 开着时读不出稳定值。
 */
static adc_channel_t gpio_to_adc1_ch(uint8_t gpio)
{
    switch (gpio) {
    case 36: return ADC_CHANNEL_0;
    case 37: return ADC_CHANNEL_1;
    case 38: return ADC_CHANNEL_2;
    case 39: return ADC_CHANNEL_3;
    case 32: return ADC_CHANNEL_4;
    case 33: return ADC_CHANNEL_5;
    case 34: return ADC_CHANNEL_6;
    case 35: return ADC_CHANNEL_7;
    default: return ADC_CHANNEL_4;      /* 兜底：GPIO32 */
    }
}

#define AUTO_WAKE_WINDOW_S 180   /* 定时唤醒后等人的窗口（秒） */

/* 电池分压 1M:1M => Vbat = Vpin * (1M+1M)/1M = Vpin * 2.0（比值与 100k:100k 相同）。
   ⚠️ 分压比是板级硬件事实：换板 / 改电阻必须同步改这里，否则电量整体偏移。
   ⚠️ 1M 高阻源（≈500k 输出阻抗）会让 ADC 读数偏低：ADC 端建议并联 100nF 到地，
      并在换板后按实测复核 BAT_FULLSCALE_MV。 */
#define BAT_DIV_NUM        200
#define BAT_DIV_DEN        100
#define BAT_ADC_MAX        4095
#define BAT_FULLSCALE_MV   3100   /* 12dB 满量程，按板实测标定（ESP32 换板后请实测复核） */
/* 未连接判定：单节锂电物理范围 + 采样极差（见 power_battery_mv）。
   R1.3.4：改为「一次调用内多次采样」，见函数注释。 */
#define BAT_MV_MIN         2400
#define BAT_MV_MAX         4600
#define BAT_SAMPLES        12     /* 每次调用的采样点数（中位数抗单个坏点） */
#define BAT_SPREAD_MAX     500    /* mV：max-min 超过即判悬空（真实电池毫秒级不会跳这么多） */

static adc_oneshot_unit_handle_t s_adc;
static TickType_t s_last_activity;
static bool s_auto_wake = false;

/* R1.2.2：唤醒脚与电池 ADC 脚改为运行时从 settings_pins() 取。
   ESP32：ADC 通道走 gpio_to_adc1_ch() 映射表，唤醒用 ext0（RTC_IO 电平）。 */
static uint8_t s_wake_pin = 27;
static adc_channel_t s_adc_ch = ADC_CHANNEL_6;   /* GPIO34 */

void power_init(void)
{
    const pin_cfg_t *p = settings_pins();
    s_wake_pin = p->v[PIN_IDX_WAKE];
    s_adc_ch   = gpio_to_adc1_ch(p->v[PIN_IDX_ADC]);

    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << s_wake_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    /* 电池 ADC：只用 ADC1（ADC2 与 WiFi 共用，不可用），通道随引脚。 */
    adc_oneshot_unit_init_cfg_t u = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&u, &s_adc));
    adc_oneshot_chan_cfg_t c = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, s_adc_ch, &c));

    ESP_LOGI(TAG, "wake btn GPIO%u, battery ADC GPIO%u (ADC1_CH%d)",
             s_wake_pin, p->v[PIN_IDX_ADC], (int)s_adc_ch);

    power_activity();
}

void power_activity(void)
{
    s_last_activity = xTaskGetTickCount();
}

void power_set_auto_wake(bool v) { s_auto_wake = v; }
bool power_auto_wake(void) { return s_auto_wake; }

uint32_t power_wakeup_causes(void)
{
    /*
     * ESP32 移植说明：esp_sleep_source_t 的取值本身就是位掩码（ESP_SLEEP_WAKEUP_ALL
     * = 0xFFFF 就是拿来按位或/与的），所以「单一起因」与「位图」是同一个数。
     * 这里统一用 esp_sleep_get_wakeup_cause()（所有 IDF 版本都有）取值，
     * 避免 esp_sleep_get_wakeup_causes()（5.2+ 才有）位图口径差异带来的歧义。
     */
    return (uint32_t)esp_sleep_get_wakeup_cause();
}

bool power_should_sleep(void)
{
    const moink_settings_t *s = settings_get();
    uint32_t limit = s->sleep_s;

    if (s_auto_wake) {
        /* 定时醒来：用短窗口等人，除非用户设了更短的空闲超时。 */
        if (limit == 0 || limit > AUTO_WAKE_WINDOW_S) limit = AUTO_WAKE_WINDOW_S;
    }
    if (limit == 0) return false;

    TickType_t idle = xTaskGetTickCount() - s_last_activity;
    return idle >= pdMS_TO_TICKS(limit * 1000);
}

void power_enter_deep_sleep(void)
{
    /* ★ 必须早于下面的 gpio_deep_sleep_hold_en()：pad hold 会锁死引脚当前电平，
       灯若在高电平会被整夜锁着点亮（几 mA × 数小时的电池账）。 */
    led_off();

    esp_wifi_stop();
    epd_panel_deep_sleep();

    /*
     * 唤醒键：内部上拉、按钮接 GND，低电平唤醒。
     *
     * ★ ESP32 与 C3 的关键差异：
     *   C3 用的是 esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown()，
     *   这个 API 在 ESP32 上不存在 —— ESP32 只有 ext0（单脚 RTC_IO 电平）与
     *   ext1（多脚掩码）。本项目只用一个按键，故用 ext0（level 0 = 低电平唤醒）。
     *   两个前提必须满足，否则深睡后唤不醒：
     *     ① 引脚先切到 RTC 域（rtc_gpio_init + INPUT_ONLY + 内部上拉）；
     *     ② RTC_PERIPH 电源域在深睡期间保持上电 —— 它的默认策略是 AUTO，
     *        一旦掉电，RTC IO 的内部上拉就没了，引脚浮空会乱触发或不触发。
     */
    rtc_gpio_init((gpio_num_t)s_wake_pin);
    rtc_gpio_set_direction((gpio_num_t)s_wake_pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)s_wake_pin);
    rtc_gpio_pulldown_dis((gpio_num_t)s_wake_pin);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup((gpio_num_t)s_wake_pin, 0));

    /* 自锁脚（MOS 一键开机）：深睡期间锁存为高电平，维持 MOS 持续导通。
       数字域深睡会掉电、普通输出会变高阻，必须用 pad hold 保持；
       上电时的首次拉高见 main.c。 */
    gpio_hold_en((gpio_num_t)PIN_SELF_LOCK);
    gpio_deep_sleep_hold_en();

    uint32_t wake_s = settings_get()->wake_s;
    if (wake_s > 0) {
        esp_sleep_enable_timer_wakeup((uint64_t)wake_s * 1000000ULL);
    }

    ESP_LOGI(TAG, "deep sleep (btn GPIO%u low, timer %lus, self-lock GPIO%d held)",
             s_wake_pin, (unsigned long)wake_s, PIN_SELF_LOCK);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_deep_sleep_start();
}

void power_self_lock_off(void)
{
    ESP_LOGW(TAG, "timeout action = off: release self-lock GPIO%d", PIN_SELF_LOCK);

    /* 自锁 = MOS 一键开机：GPIO13（ESP32）输出高 → MOS 导通维持供电，拉低即断电。
       有自锁电路时约 100 ms 内失电，下面的代码不会执行到。 */
    gpio_set_direction((gpio_num_t)PIN_SELF_LOCK, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_SELF_LOCK, 0);

    /* 无自锁电路（USB 供电 / 未装 MOS）时拉低无效：1 s 后退回深睡，
       避免设备一直空转耗电（此时 hold 会锁住低电平，MOS 若存在仍保持关断）。 */
    vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGW(TAG, "self-lock low did not cut power (no self-lock circuit?), "
                  "fallback to deep sleep");
    power_enter_deep_sleep();
}

int power_battery_mv(void)
{
    /* R1.3.4（FB-019）：**一次调用内完成全部采样**。
       旧实现要求「跨请求连续 3 次有效」才返回数值，而 /api/info 每次只调本函数一次、
       控制页又只在打开时请求一次 ⇒ 计数器永远到不了 3，首次打开必显示「未连接」，
       必须手动刷新数次才偶发出值（实测复现）。
       新做法：
       1. 连采 BAT_SAMPLES 次，取**中位数**（单个坏点不影响结果）；
       2. 用**极差 max-min** 判悬空：ADC 悬空时读数靠杂散耦合乱漂
          （实测 3.2/3.3/17.6V 交替），极差远大于真实电池；
       3. 中位数越界或极差超限 → 返回 -1（页面显示「未连接」）。
       这样单次 /api/info 即可出稳定电压，页面无需轮询
       （轮询会让 power_activity() 不断续命、永不休眠）。 */
    int v[BAT_SAMPLES];
    int n = 0;

    for (int i = 0; i < BAT_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, s_adc_ch, &raw) != ESP_OK) continue;
        /* 注意乘法顺序：先除再乘，避免 raw*3100*200 溢出 int */
        v[n++] = raw * BAT_FULLSCALE_MV / BAT_ADC_MAX * BAT_DIV_NUM / BAT_DIV_DEN;
    }
    if (n < 3) return -1;

    /* 插入排序（最多 12 个元素），取中位数与极差 */
    for (int i = 1; i < n; i++) {
        int t = v[i], j = i - 1;
        while (j >= 0 && v[j] > t) { v[j + 1] = v[j]; j--; }
        v[j + 1] = t;
    }
    int med = v[n / 2];
    int spread = v[n - 1] - v[0];

    if (med < BAT_MV_MIN || med > BAT_MV_MAX) return -1;
    if (spread > BAT_SPREAD_MAX) return -1;
    return med;
}
