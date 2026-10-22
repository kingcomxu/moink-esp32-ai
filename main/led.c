#include "led.h"
#include "settings.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "led";

/*
 * 节拍：开机快闪半周期 70 ms（≈7 Hz），忙时 100 ms（≈5 Hz）。
 * 两者肉眼可分辨：启动中节奏更急，刷屏时稍缓，不必看串口也知道设备在干什么。
 * 常亮 / 熄灭两态每 200 ms 轮询一次状态，切态最迟 200 ms 生效。
 */
#define LED_BOOT_HALF_MS  70
#define LED_BUSY_HALF_MS  100
#define LED_POLL_MS       200
#define LED_TASK_STACK    2048
#define LED_TASK_PRIO     2

typedef enum {
    LED_ST_BOOT = 0,   /* 上电自检：快闪 */
    LED_ST_READY,      /* 启动完成：常亮 */
    LED_ST_OFF,        /* 熄灭（深睡 / 关机） */
} led_state_t;

static volatile led_state_t s_state = LED_ST_BOOT;
static volatile int         s_busy  = 0;
/* s_busy 会被 httpd 任务（收包）与 frame 任务（刷屏）同时增减，
   用临界区保护，避免丢更新后计数卡在 >0、灯一直闪。 */
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

static inline void led_hw(int on)
{
    gpio_set_level((gpio_num_t)PIN_STATUS_LED,
                   on ? PIN_STATUS_LED_ACTIVE : !PIN_STATUS_LED_ACTIVE);
}

static bool led_is_busy(void)
{
    taskENTER_CRITICAL(&s_mux);
    int b = s_busy;
    taskEXIT_CRITICAL(&s_mux);
    return b > 0;
}

static void led_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* 「忙」优先于常亮/熄灭：刷屏期间不管当前是什么态都要闪。 */
        if (led_is_busy()) {
            led_hw(1);
            vTaskDelay(pdMS_TO_TICKS(LED_BUSY_HALF_MS));
            led_hw(0);
            vTaskDelay(pdMS_TO_TICKS(LED_BUSY_HALF_MS));
            continue;
        }
        switch (s_state) {
        case LED_ST_BOOT:
            led_hw(1);
            vTaskDelay(pdMS_TO_TICKS(LED_BOOT_HALF_MS));
            led_hw(0);
            vTaskDelay(pdMS_TO_TICKS(LED_BOOT_HALF_MS));
            break;
        case LED_ST_READY:
            led_hw(1);
            vTaskDelay(pdMS_TO_TICKS(LED_POLL_MS));
            break;
        default:
            led_hw(0);
            vTaskDelay(pdMS_TO_TICKS(LED_POLL_MS));
            break;
        }
    }
}

void led_init(void)
{
#if PIN_STATUS_LED >= 0
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_STATUS_LED,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    led_hw(0);
    s_state = LED_ST_BOOT;
    if (xTaskCreate(led_task, "led", LED_TASK_STACK, NULL,
                    LED_TASK_PRIO, NULL) != pdPASS) {
        ESP_LOGE(TAG, "led task create failed");
        return;
    }
    ESP_LOGI(TAG, "status led GPIO%d (active %s)", PIN_STATUS_LED,
             PIN_STATUS_LED_ACTIVE ? "high" : "low");
#else
    ESP_LOGI(TAG, "status led disabled (PIN_STATUS_LED < 0)");
#endif
}

void led_ready(void)
{
#if PIN_STATUS_LED >= 0
    s_state = LED_ST_READY;
    led_hw(1);      /* 立刻常亮，不等任务下一个轮询周期 */
#endif
}

void led_off(void)
{
#if PIN_STATUS_LED >= 0
    s_state = LED_ST_OFF;
    led_hw(0);
#endif
}

void led_busy_enter(void)
{
#if PIN_STATUS_LED >= 0
    taskENTER_CRITICAL(&s_mux);
    s_busy++;
    taskEXIT_CRITICAL(&s_mux);
#endif
}

void led_busy_exit(void)
{
#if PIN_STATUS_LED >= 0
    taskENTER_CRITICAL(&s_mux);
    if (s_busy > 0) s_busy--;
    taskEXIT_CRITICAL(&s_mux);
#endif
}
