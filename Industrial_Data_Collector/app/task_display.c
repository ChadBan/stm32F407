/**
 * @file    task_display.c
 * @brief   显示任务：消费采集队列，按当前页面刷新 OLED（主界面/时间/参数/状态）
 *
 * 低优先级；vTaskDelayUntil 周期刷新，队列有更新即画最新一帧。
 * 字体仅 A-Z/0-9 及少量符号，故所有标签用大写英文。
 */
#include "app_tasks.h"
#include "app_state.h"
#include "dev_oled.h"
#include "dev_w25q.h"
#include "bsp_wdg.h"
#include "util_time.h"
#include <stdio.h>

/* x1000 温度 -> "23.4"（含负号） */
static void fmt_temp10(int32_t t1000, char *buf, size_t cap)
{
    snprintf(buf, cap, "%d.%d", (int)(t1000 / 1000),
             (int)((t1000 < 0 ? -t1000 : t1000) / 100 % 10));
}

static const char *BAUD_STR[5] = {"9600", "19200", "38400", "57600", "115200"};

static void draw_main(const collect_sample_t *s)
{
    char line[20];
    oled_clear();
    oled_puts(0, 8, "T:");
    fmt_temp10(s->temp_x1000, line, sizeof(line));
    oled_puts(18, 8, line);
    oled_puts(66, 8, "C");

    oled_puts(0, 20, "H:");
    snprintf(line, sizeof(line), "%d.%d", (int)(s->humi_x1000 / 1000),
             (int)(s->humi_x1000 / 100 % 10));
    oled_puts(18, 20, line);
    oled_puts(66, 20, "%");

    oled_puts(0, 32, "BAT");
    snprintf(line, sizeof(line), "%dMV", (int)s->battery_mv);
    oled_puts(24, 32, line);

    /* 无有效温度数据时提示错误，便于现场排查传感器 */
    if (!(s->valid_bits & PARAM_VALID_BITS_TEMP))
        oled_puts(0, 44, "SENS ERR");
    oled_flush();
}

static void draw_time(const collect_sample_t *s)
{
    char buf[20];
    oled_clear();
    oled_puts(0, 0, "TIME");
    if (s->valid_bits & VALID_BITS_TIME)
    {
        dt_format(&s->ts, buf, sizeof(buf));
        oled_puts(0, 16, buf);
    }
    else
    {
        oled_puts(0, 16, "RTC ERR");
    }
    oled_flush();
}

static void draw_param(void)
{
    device_params_t p;
    app_get_params(&p);
    uint8_t field = app_get_param_field();
    char line[20];
    const uint8_t row_y[5] = {12, 22, 32, 42, 52};

    oled_clear();
    oled_puts(48, 0, "SET");

    fmt_temp10((int32_t)p.temp_alarm_hi * 100, line, sizeof(line));
    oled_puts(8, row_y[0], "T.HI");
    oled_puts(48, row_y[0], line);
    fmt_temp10((int32_t)p.temp_alarm_lo * 100, line, sizeof(line));
    oled_puts(8, row_y[1], "T.LO");
    oled_puts(48, row_y[1], line);
    snprintf(line, sizeof(line), "BAT %d", (int)p.batt_alarm_mv);
    oled_puts(8, row_y[2], line);
    snprintf(line, sizeof(line), "ADR %d", (int)p.modbus_addr);
    oled_puts(8, row_y[3], line);
    snprintf(line, sizeof(line), "BAUD %s", BAUD_STR[p.baud_code > 4 ? 0 : p.baud_code]);
    oled_puts(8, row_y[4], line);

    /* 光标：选中行左侧画短横 */
    oled_hline(0, (uint8_t)(row_y[field] + 6), 6);
    oled_flush();
}

static void draw_status(void)
{
    uint8_t a = app_get_alarm_bits();
    char line[20];
    oled_clear();
    oled_puts(0, 0, "STATUS");
    if (a == 0)
    {
        oled_puts(0, 14, "NORMAL");
    }
    else
    {
        oled_puts(0, 14, "ALARM");
        /* 各行错开，避免同坐标互相覆盖 */
        if (a & ALM_BIT_TEMP_HI)
            oled_puts(0, 26, "TEMP HI");
        if (a & ALM_BIT_TEMP_LO)
            oled_puts(0, 38, "TEMP LO");
        if (a & ALM_BIT_BATT_LO)
            oled_puts(0, 50, "BATT LO");
        if (a & ALM_BIT_SENSOR)
            oled_puts(72, 26, "SENS");
    }
    /* 历史日志已存条数（仅读 RTC 备份寄存器，无 SPI 开销） */
    snprintf(line, sizeof(line), "LOG %d", (int)w25q_log_count());
    oled_puts(0, 56, line);
    oled_flush();
}

void display_task(void *arg)
{
    (void)arg;
    collect_sample_t s = {0};
    TickType_t last = xTaskGetTickCount();
    for (;;)
    {
        bool got = false;
        while (xQueueReceive(app_data_queue(), &s, 0) == pdTRUE)
            got = true;
        if (!got)
            app_get_last_sample(&s);

        switch (app_get_page())
        {
        case PAGE_MAIN:
            draw_main(&s);
            break;
        case PAGE_TIME:
            draw_time(&s);
            break;
        case PAGE_PARAM:
            draw_param();
            break;
        default:
            draw_status();
            break;
        }

        bsp_wdg_task_feed(WDG_TASK_DISPLAY);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
    }
}
