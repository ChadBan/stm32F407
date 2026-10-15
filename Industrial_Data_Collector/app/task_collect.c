/**
 * @file    task_collect.c
 * @brief   采集任务：ADC(DMA) + SHT30，滤波后打时间戳入队，并周期落盘历史日志
 *
 * 周期 500ms；禁止在此做长阻塞硬件等待（I2C 用短超时、ADC 走 DMA 快照）。
 * 日志按 LOG_EVERY_N 次采样写一条(默认 10 次 = 5s)，避免 500ms 高频擦写 Flash。
 */
#include "app_tasks.h"
#include "app_state.h"
#include "bsp_adc.h"
#include "dev_sht30.h"
#include "dev_w25q.h"
#include "bsp_rtc.h"
#include "bsp_wdg.h"
#include "util_filter.h"
#include "util_time.h"

#define LOG_EVERY_N 10 /* 每 10 个采样点(5s)落一条历史日志 */

void collect_task(void *arg)
{
    (void)arg;
    static moving_avg_t f_temp, f_humi, f_batt, f_s1, f_s2, f_cur;
    ma_init(&f_temp, 5);
    ma_init(&f_humi, 5);
    ma_init(&f_batt, 8);
    ma_init(&f_s1, 4);
    ma_init(&f_s2, 4);
    ma_init(&f_cur, 8);

    uint32_t cycle = 0;
    TickType_t last = xTaskGetTickCount();
    for (;;)
    {
        collect_sample_t s = {0};

        /* ADC：DMA 快照 -> 转物理量 + 滤波 */
        uint16_t raw[ADC_CH_COUNT];
        bsp_adc_snapshot(raw);
        /* 用同一份快照算电压，勿再调 bsp_adc_battery_mv(内部会二次快照导致时间不一致) */
        s.battery_mv = (uint32_t)ma_push(&f_batt,
                        (int32_t)(bsp_adc_raw_to_mv(raw[ADC_CH_BATTERY]) * 100u / BATTERY_DIVIDER_X100));
        s.sensor1_raw = (uint16_t)ma_push(&f_s1, (int32_t)raw[ADC_CH_SENSOR1]);
        s.sensor2_raw = (uint16_t)ma_push(&f_s2, (int32_t)raw[ADC_CH_SENSOR2]);
        s.current_mv = (uint16_t)ma_push(&f_cur, (int32_t)bsp_adc_raw_to_mv(raw[ADC_CH_CURRENT]));
        s.valid_bits |= PARAM_VALID_BITS_BATT | VALID_BITS_SENSOR;

        /* SHT30：I2C 短超时；失败不阻塞，valid_bits 如实反映 */
        sht30_sample_t t;
        if (sht30_read(&t) && t.valid)
        {
            s.temp_x1000 = ma_push(&f_temp, t.temp_x1000);
            s.humi_x1000 = (uint32_t)ma_push(&f_humi, (int32_t)t.humi_x1000);
            s.valid_bits |= PARAM_VALID_BITS_TEMP | PARAM_VALID_BITS_HUMI;
        }

        /* RTC 时间戳 */
        bool time_ok = bsp_rtc_get(&s.ts);
        s.valid_bits |= time_ok ? VALID_BITS_TIME : 0;

        /* 更新最新值 + 投递队列（非阻塞） */
        app_update_sample(&s);
        xQueueSend(app_data_queue(), &s, 0);

        /* 周期落盘历史日志（带当时报警位快照） */
        if ((cycle % LOG_EVERY_N) == 0)
        {
            w25q_log_rec_t rec = {
                .epoch = time_ok ? dt_to_epoch(&s.ts) : 0,
                .temp_x1000 = s.temp_x1000,
                .humi_x1000 = s.humi_x1000,
                .batt_mv = s.battery_mv,
                .sensor1 = s.sensor1_raw,
                .sensor2 = s.sensor2_raw,
                .current_mv = s.current_mv,
                .valid_bits = s.valid_bits,
                .alarm_bits = app_get_alarm_bits(),
            };
            w25q_log_append(&rec);
        }
        cycle++;

        bsp_wdg_task_feed(WDG_TASK_COLLECT);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(COLLECT_PERIOD_MS));
    }
}
