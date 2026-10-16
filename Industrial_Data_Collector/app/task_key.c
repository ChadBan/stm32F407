/**
 * @file    task_key.c
 * @brief   按键任务：EXTI 边沿标志 + 软件去抖，切换页面 / 修改报警阈值
 *
 * ISR 只置标志（见 bsp_gpio），本任务周期扫描去抖，避免在中断里做业务处理。
 * 参数编辑光标经 app_state 与显示任务共享；任何按键都刷新活动时间(抑制休眠)。
 */
#include "app_tasks.h"
#include "app_state.h"
#include "bsp_gpio.h"
#include "bsp_wdg.h"

#define KEY_DEBOUNCE_MS 30
#define PARAM_FIELD_COUNT 5

void key_task(void *arg)
{
    (void)arg;
    uint32_t last_ms[KEY_ID_MAX] = {0};

    for (;;)
    {
        for (int k = 0; k < KEY_ID_MAX; k++)
        {
            if (!bsp_key_take_edge((key_id_t)k))
                continue;
            uint32_t now = HAL_GetTick();
            if (now - last_ms[k] < KEY_DEBOUNCE_MS)
                continue; /* 去抖 */
            last_ms[k] = now;
            if (!bsp_key_pressed((key_id_t)k))
                continue; /* 抖动 */

            app_mark_activity();
            disp_page_t page = app_get_page();
            bool ok = (k == KEY_OK);
            bool up = (k == KEY_UP);
            bool down = (k == KEY_DOWN);

            if (page != PAGE_PARAM)
            {
                /* 主界面/时间页：OK 切页；UP/DOWN 快捷切页 */
                if (ok)
                    app_set_page((disp_page_t)((page + 1) % PAGE_COUNT));
                else if (up)
                    app_set_page((disp_page_t)((page + PAGE_COUNT - 1) % PAGE_COUNT));
                else if (down)
                    app_set_page((disp_page_t)((page + 1) % PAGE_COUNT));
                continue;
            }

            /* 参数页：UP/DOWN 改当前字段并即时落盘，OK 移动光标 */
            uint8_t field = app_get_param_field();
            device_params_t p;
            app_get_params(&p);
            bool changed = false;

            if (up || down)
            {
                int dir = up ? 1 : -1;
                switch (field)
                {
                case 0:
                {
                    /* 温度阈值限幅 -40.0~125.0C(x10)，防止反复按键 int16 溢出回绕 */
                    int32_t v = (int32_t)p.temp_alarm_hi + 5 * dir;
                    if (v >= -400 && v <= 1250)
                    {
                        p.temp_alarm_hi = (int16_t)v;
                        changed = true;
                    }
                }
                break;
                case 1:
                {
                    int32_t v = (int32_t)p.temp_alarm_lo + 5 * dir;
                    if (v >= -400 && v <= 1250)
                    {
                        p.temp_alarm_lo = (int16_t)v;
                        changed = true;
                    }
                }
                break;
                case 2:
                {
                    int32_t v = (int32_t)p.batt_alarm_mv + 50 * dir;
                    if (v >= 2500 && v <= 5000)
                    {
                        p.batt_alarm_mv = (uint16_t)v;
                        changed = true;
                    }
                }
                break;
                case 3:
                {
                    int32_t v = (int32_t)p.modbus_addr + dir;
                    if (v >= 1 && v <= 247)
                    {
                        p.modbus_addr = (uint8_t)v;
                        changed = true;
                    }
                }
                break;
                case 4:
                {
                    int32_t v = (int32_t)p.baud_code + dir;
                    if (v >= 0 && v <= 4)
                    {
                        p.baud_code = (uint8_t)v;
                        changed = true;
                    }
                }
                break;
                }
                if (changed)
                    app_set_params(&p);
            }
            else if (ok)
            {
                /* 光标前进；回到起点时保存一次，并同步波特率到通讯任务(见 comm 读取参数) */
                app_set_param_field((uint8_t)((field + 1) % PARAM_FIELD_COUNT));
                app_set_params(&p);
            }
        }

        bsp_wdg_task_feed(WDG_TASK_KEY);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
