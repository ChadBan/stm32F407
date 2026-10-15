/**
 * @file    bsp_pwm.c
 * @brief   蜂鸣器 PWM 实现
 */
#include "bsp_pwm.h"

/* F407 TIM4 挂载 APB1，定时器时钟 = 84MHz（APB1 预分频非1时 x2） */
#define TIM_CLK_HZ 84000000u
#define PWM_PRESCALER (100u - 1u) /* 计数器时钟 = 840kHz */

void bsp_pwm_init(void)
{
    /* CubeMX 已完成 htim_pwm 基础配置，此处设置默认频率并启动 */
    bsp_pwm_set_freq(BUZZER_PWM_PERIOD_HZ);
    HAL_TIM_PWM_Start(&htim_pwm, BUZZER_TIM_CH);
    bsp_pwm_beep(false);
}

void bsp_pwm_set_freq(uint32_t freq_hz)
{
    if (freq_hz == 0)
        return;
    uint32_t cnt_clk = TIM_CLK_HZ / (PWM_PRESCALER + 1u);
    uint32_t arr = cnt_clk / freq_hz;
    if (arr == 0)
        arr = 1;
    /* ARR 与 CCR 更新：50% 占空比 */
    __HAL_TIM_SET_AUTORELOAD(&htim_pwm, arr - 1);
    __HAL_TIM_SET_COMPARE(&htim_pwm, BUZZER_TIM_CH, arr / 2);
}

void bsp_pwm_beep(bool on)
{
    /* 用实际 ARR 计算 50% 占空比；Init.Period 在 set_freq 改频后不会同步更新 */
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim_pwm);
    __HAL_TIM_SET_COMPARE(&htim_pwm, BUZZER_TIM_CH, on ? (arr / 2u) : 0u);
}
