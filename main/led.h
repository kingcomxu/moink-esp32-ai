#ifndef MOINK_LED_H
#define MOINK_LED_H

/*
 * 板载状态灯（蓝色 LED，脚位与极性见 settings.h 的 PIN_STATUS_LED /
 * PIN_STATUS_LED_ACTIVE）。
 *
 * 语义（对外只有这四种「状态」，波形由后台任务自己产生）：
 *   上电 / 深睡唤醒  快速闪烁 —— app_main 第一件事就进入，标识固件在跑；
 *   启动完成         常亮     —— app_main 末尾把状态切到 READY；
 *   传图 / 刷屏 / 清屏 闪烁    —— frame 模块在收包与整屏写入期间置「忙」；
 *   深睡前           熄灭     —— 必须早于 gpio_deep_sleep_hold_en()，
 *                              否则 pad hold 会把灯锁在高电平整夜耗电。
 *
 * 接口只改状态、不产生延时，因此可以从任意任务 / httpd 上下文调用，
 * 不会把刷屏或 HTTP 应答卡在闪烁节拍上。
 */

void led_init(void);        /* 配引脚 + 起任务，进入「启动中」快闪 */
void led_ready(void);       /* 启动完成：常亮 */
void led_busy_enter(void);  /* 进入「忙」（闪烁）；与 exit 配对，可嵌套 */
void led_busy_exit(void);
void led_off(void);         /* 熄灭（深睡 / 关机前） */

#endif /* MOINK_LED_H */
