/**
 * @file    task_alarm.c
 * @brief   报警任务：阈值判定 -> PWM 蜂鸣器，支持消音；兼任看门狗监督
 *
 * 报警优先级高。蜂鸣用间歇鸣响(鸣/停交替)而非长鸣，省电且更醒目。
 * 本任务额外调用 bsp_wdg_poll()：核对所有任务心跳，全部健康才喂 IWDG。
 */
#include "app_tasks.h"
#include "app_state.h"
#include "bsp_pwm.h"
#include "bsp_wdg.h"
#include "bsp_uart.h"

#define BUZZ_ON_MS 120
#define BUZZ_OFF_MS 400
#define ALARM_SCAN_MS 100
#define WDG_TASK_TIMEOUT_MS 2000

void alarm_task(void *arg)
{
    (void)arg;
    bool buzz_phase = false;
    uint32_t last_toggle = 0;
    uint32_t sht_fail_cnt = 0; /* SHT30 连续无有效温度周期数 */

    for (;;)
    {
        device_params_t p;
        app_get_params(&p);
        collect_sample_t s;
        bool have = app_get_last_sample(&s);

        uint8_t bits = 0;
        if (have && (s.valid_bits & PARAM_VALID_BITS_TEMP))
        {
            sht_fail_cnt = 0;
            int16_t temp_x10 = (int16_t)(s.temp_x1000 / 100);
            if (temp_x10 > p.temp_alarm_hi)
                bits |= ALM_BIT_TEMP_HI;
            if (temp_x10 < p.temp_alarm_lo)
                bits |= ALM_BIT_TEMP_LO;
        }
        else if (sht_fail_cnt < UINT32_MAX)
        {
            sht_fail_cnt++;
        }
        /* 连续 30 个周期(3s)无有效温湿度 -> 判定传感器故障 */
        if (sht_fail_cnt >= 30)
            bits |= ALM_BIT_SENSOR;
        if (have && (s.valid_bits & PARAM_VALID_BITS_BATT))
        {
            if (s.battery_mv > 0 && s.battery_mv < p.batt_alarm_mv)
                bits |= ALM_BIT_BATT_LO;
        }
        app_set_alarm_bits(bits);

        bool alarming = (bits != 0) && !p.mute;
        if (alarming)
        {
            uint32_t now = HAL_GetTick();
            if (now - last_toggle >= (buzz_phase ? BUZZ_ON_MS : BUZZ_OFF_MS))
            {
                buzz_phase = !buzz_phase;
                last_toggle = now;
                bsp_pwm_beep(buzz_phase);
            }
        }
        else
        {
            bsp_pwm_beep(false);
            buzz_phase = false;
        }

        /* 看门狗监督：所有任务心跳健康才真正刷新 IWDG */
        bsp_wdg_poll(WDG_TASK_TIMEOUT_MS);
        bsp_wdg_task_feed(WDG_TASK_ALARM);

        vTaskDelay(pdMS_TO_TICKS(ALARM_SCAN_MS));
    }
}
