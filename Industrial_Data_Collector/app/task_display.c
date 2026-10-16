/**
 * @file    task_display.c
 * @brief   显示任务：消费采集队列，按当前页面刷新 TFT LCD（主界面/时间/参数/状态）
 *
 * 低优先级；vTaskDelayUntil 周期刷新，队列有更新即画最新一帧。
 * 屏幕为 480x320，8x16 字符（每字符 8 像素宽），标签一律大写英文。
 */
#include "app_tasks.h"
#include "app_state.h"
#include "dev_lcd.h"
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
    char line[24];
    lcd_clear(COLOR_BLACK);

    lcd_show_string(8, 32, "T:", COLOR_YELLOW, COLOR_BLACK);
    fmt_temp10(s->temp_x1000, line, sizeof(line));
    lcd_show_string(32, 32, line, COLOR_WHITE, COLOR_BLACK);
    lcd_show_string(80, 32, "C", COLOR_YELLOW, COLOR_BLACK);

    lcd_show_string(8, 96, "H:", COLOR_YELLOW, COLOR_BLACK);
    snprintf(line, sizeof(line), "%d.%d", (int)(s->humi_x1000 / 1000),
             (int)(s->humi_x1000 / 100 % 10));
    lcd_show_string(32, 96, line, COLOR_WHITE, COLOR_BLACK);
    lcd_show_string(80, 96, "%", COLOR_YELLOW, COLOR_BLACK);

    lcd_show_string(8, 160, "BAT:", COLOR_YELLOW, COLOR_BLACK);
    snprintf(line, sizeof(line), "%dMV", (int)s->battery_mv);
    lcd_show_string(48, 160, line, COLOR_WHITE, COLOR_BLACK);

    /* 无有效温度数据时提示错误，便于现场排查传感器 */
    if (!(s->valid_bits & PARAM_VALID_BITS_TEMP))
        lcd_show_string(8, 224, "SENS ERR", COLOR_RED, COLOR_BLACK);
}

static void draw_time(const collect_sample_t *s)
{
    char buf[32];
    lcd_clear(COLOR_BLACK);
    lcd_show_string(8, 32, "TIME", COLOR_YELLOW, COLOR_BLACK);
    if (s->valid_bits & VALID_BITS_TIME)
    {
        dt_format(&s->ts, buf, sizeof(buf));
        lcd_show_string(8, 96, buf, COLOR_WHITE, COLOR_BLACK);
    }
    else
    {
        lcd_show_string(8, 96, "RTC ERR", COLOR_RED, COLOR_BLACK);
    }
}

static void draw_param(void)
{
    device_params_t p;
    app_get_params(&p);
    uint8_t field = app_get_param_field();
    char line[24];
    const uint8_t row_y[5] = {64, 96, 128, 160, 192};

    lcd_clear(COLOR_BLACK);
    lcd_show_string(8, 24, "SET", COLOR_YELLOW, COLOR_BLACK);

    fmt_temp10((int32_t)p.temp_alarm_hi * 100, line, sizeof(line));
    lcd_show_string(8, row_y[0], "T.HI", COLOR_WHITE, COLOR_BLACK);
    lcd_show_string(64, row_y[0], line, COLOR_CYAN, COLOR_BLACK);

    fmt_temp10((int32_t)p.temp_alarm_lo * 100, line, sizeof(line));
    lcd_show_string(8, row_y[1], "T.LO", COLOR_WHITE, COLOR_BLACK);
    lcd_show_string(64, row_y[1], line, COLOR_CYAN, COLOR_BLACK);

    snprintf(line, sizeof(line), "BAT %d", (int)p.batt_alarm_mv);
    lcd_show_string(8, row_y[2], line, COLOR_WHITE, COLOR_BLACK);

    snprintf(line, sizeof(line), "ADR %d", (int)p.modbus_addr);
    lcd_show_string(8, row_y[3], line, COLOR_WHITE, COLOR_BLACK);

    snprintf(line, sizeof(line), "BAUD %s", BAUD_STR[p.baud_code > 4 ? 0 : p.baud_code]);
    lcd_show_string(8, row_y[4], line, COLOR_WHITE, COLOR_BLACK);

    /* 光标：选中行左侧画一竖条色块 */
    if (field < 5)
        lcd_fill(0, row_y[field], 4, (uint16_t)(row_y[field] + 15), COLOR_YELLOW);
}

static void draw_status(void)
{
    uint8_t a = app_get_alarm_bits();
    char line[24];
    lcd_clear(COLOR_BLACK);
    lcd_show_string(8, 32, "STATUS", COLOR_YELLOW, COLOR_BLACK);
    if (a == 0)
    {
        lcd_show_string(8, 96, "NORMAL", COLOR_GREEN, COLOR_BLACK);
    }
    else
    {
        lcd_show_string(8, 96, "ALARM", COLOR_RED, COLOR_BLACK);
        uint8_t y = 128;
        if (a & ALM_BIT_TEMP_HI) { lcd_show_string(8, y, "TEMP HI", COLOR_RED, COLOR_BLACK); y += 24; }
        if (a & ALM_BIT_TEMP_LO) { lcd_show_string(8, y, "TEMP LO", COLOR_RED, COLOR_BLACK); y += 24; }
        if (a & ALM_BIT_BATT_LO) { lcd_show_string(8, y, "BATT LO", COLOR_RED, COLOR_BLACK); y += 24; }
        if (a & ALM_BIT_SENSOR)  { lcd_show_string(8, y, "SENS", COLOR_RED, COLOR_BLACK); }
    }

    /* 历史日志已存条数（仅读 RTC 备份寄存器，无 SPI 开销） */
    snprintf(line, sizeof(line), "LOG %d", (int)w25q_log_count());
    lcd_show_string(8, 288, line, COLOR_GRAY, COLOR_BLACK);
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
