/**
 * @file    bsp_pwm.h
 * @brief   TIM4-CH1 PWM 驱动无源蜂鸣器（报警音）
 */
#ifndef BSP_PWM_H
#define BSP_PWM_H

#include "board_config.h"

void bsp_pwm_init(void);
void bsp_pwm_beep(bool on);              /* 开关蜂鸣（50% 占空比方波） */
void bsp_pwm_set_freq(uint32_t freq_hz); /* 动态改频率(变调) */

#endif /* BSP_PWM_H */
