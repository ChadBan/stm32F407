/**
 * @file    bsp_gpio.h
 * @brief   板级 GPIO：运行指示灯、按键 EXTI 去抖状态机
 *
 * 按键采用 EXTI 中断触发 + 任务侧扫描去抖：ISR 仅记录“哪个键刚被触发”，
 * 由 key_task 周期性读取 GPIO 电平做去抖与长按判定，避免在中断里做耗时处理。
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

#include "board_config.h"

void bsp_gpio_init(void);

/* LED 控制 */
void bsp_led_set(bool on);
void bsp_led_toggle(void);

/* 读取按键原始电平（已按硬件有效方向归一化：按下返回 true） */
bool bsp_key_pressed(key_id_t id);

/* ISR 置位、任务消费：返回某键在上一周期是否产生过边沿 */
bool bsp_key_take_edge(key_id_t id);

#endif /* BSP_GPIO_H */
