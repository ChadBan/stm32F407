/**
 * @file    task_comm.c
 * @brief   通讯任务：RS485 Modbus-RTU 从机（遥测读取 + 参数/校时写入 + 历史日志）
 *
 * 唤醒：IDLE/DMA 中断给信号量 -> 环形缓冲取帧 -> 协议栈处理 -> 回帧。
 *
 * 寄存器映射：
 *  输入寄存器(0x04) 只读：
 *    0 温度x10  1 湿度x10  2 电池mV  3 sensor1  4 sensor2  5 电流采样电压mV
 *    6 年  7 (月<<8|日)  8 (时<<8|分)  9 秒  10 日志总条数
 *    11.. 游标所选历史记录：11 epochHi 12 epochLo 13 tempX10 14 humiX10
 *                           15 batt 16 sensor1 17 sensor2 18 current 19 (valid<<8|alarm)
 *  保持寄存器(0x03/0x06/0x10)：
 *    0 从机地址  1 温度上限x10  2 温度下限x10  3 电池阈值mV  4 波特率码  5 消音
 *    6 日志游标(写后自动预取该记录)
 *    0x10 年  0x11 (月<<8|日)  0x12 (时<<8|分)  0x13 秒  0x14 写1=应用校时
 */
#include "app_tasks.h"
#include "app_state.h"
#include "bsp_uart.h"
#include "dev_modbus.h"
#include "dev_w25q.h"
#include "bsp_rtc.h"
#include "bsp_wdg.h"
#include "util_time.h"
#include "task.h"
#include <string.h>

#define RX_FRAME_MAX 256
#define TX_FRAME_MAX 256

#define HR_LOG_CURSOR 6
#define HR_TIME_BASE 0x10
#define HR_TIME_APPLY 0x14

/* 仅通讯任务访问，无需加锁 */
static w25q_log_rec_t g_rec; /* 游标预取的历史记录 */
static bool g_rec_valid = false;
static dt_t g_time_stage;

static void prefetch_cursor(uint16_t idx)
{
    g_rec_valid = w25q_log_read(idx, &g_rec);
}

/* ---- 读输入寄存器 ---- */
static bool mb_read_input(uint16_t reg, uint16_t *out)
{
    if (reg <= 9)
    {
        collect_sample_t s;
        bool has = app_get_last_sample(&s);
        if (!has)
            memset(&s, 0, sizeof(s));
        switch (reg)
        {
        case 0:
            *out = (uint16_t)(s.temp_x1000 / 100);
            break;
        case 1:
            *out = (uint16_t)(s.humi_x1000 / 100);
            break;
        case 2:
            *out = (uint16_t)(s.battery_mv > 0xFFFF ? 0xFFFF : s.battery_mv);
            break;
        case 3:
            *out = s.sensor1_raw;
            break;
        case 4:
            *out = s.sensor2_raw;
            break;
        case 5:
            *out = s.current_mv;
            break;
        case 6:
            *out = s.ts.year;
            break;
        case 7:
            *out = (uint16_t)((s.ts.month << 8) | s.ts.day);
            break;
        case 8:
            *out = (uint16_t)((s.ts.hour << 8) | s.ts.minute);
            break;
        default:
            *out = s.ts.second;
            break;
        }
        return true;
    }
    if (reg == 10)
    {
        *out = (uint16_t)w25q_log_count();
        return true;
    }

    /* 11..19 历史记录（按游标预取） */
    if (reg >= 11 && reg <= 19)
    {
        if (!g_rec_valid)
        {
            *out = 0;
            return true;
        } /* 无有效记录返回 0，不报异常 */
        uint32_t ehi = g_rec.epoch >> 16, elo = g_rec.epoch & 0xFFFF;
        uint16_t vals[] = {
            (uint16_t)ehi, (uint16_t)elo,
            (uint16_t)(g_rec.temp_x1000 / 100),
            (uint16_t)(g_rec.humi_x1000 / 100),
            (uint16_t)g_rec.batt_mv,
            g_rec.sensor1, g_rec.sensor2, g_rec.current_mv,
            (uint16_t)((g_rec.valid_bits << 8) | g_rec.alarm_bits)};
        *out = vals[reg - 11];
        return true;
    }
    return false; /* 非法地址 */
}

/* ---- 读保持寄存器 ---- */
static bool mb_read_holding(uint16_t reg, uint16_t *out)
{
    device_params_t p;
    app_get_params(&p);
    switch (reg)
    {
    case 0:
        *out = p.modbus_addr;
        break;
    case 1:
        *out = (uint16_t)p.temp_alarm_hi;
        break;
    case 2:
        *out = (uint16_t)p.temp_alarm_lo;
        break;
    case 3:
        *out = p.batt_alarm_mv;
        break;
    case 4:
        *out = p.baud_code;
        break;
    case 5:
        *out = p.mute;
        break;
    case 7:
        *out = p.sleep_en;
        break;
    default:
        return false;
    }
    return true;
}

/* ---- 写保持寄存器 ---- */
static bool mb_write_holding(uint16_t reg, uint16_t val)
{
    /* 校时暂存区 */
    if (reg >= HR_TIME_BASE && reg <= 0x13)
    {
        switch (reg)
        {
        case 0x10:
            g_time_stage.year = val;
            break;
        case 0x11:
            g_time_stage.month = val >> 8;
            g_time_stage.day = val & 0xFF;
            break;
        case 0x12:
            g_time_stage.hour = val >> 8;
            g_time_stage.minute = val & 0xFF;
            break;
        case 0x13:
            g_time_stage.second = val;
            break;
        }
        return true;
    }
    if (reg == HR_TIME_APPLY)
    {
        if (val == 1)
            return bsp_rtc_set(&g_time_stage); /* 合法性由 bsp_rtc_set 校验 */
        return true;
    }
    if (reg == HR_LOG_CURSOR)
    {
        prefetch_cursor(val);
        return true;
    }

    /* 设备参数 */
    device_params_t p;
    app_get_params(&p);
    switch (reg)
    {
    case 0:
        if (val == 0 || val > 247)
            return false;
        p.modbus_addr = (uint8_t)val;
        break;
    case 1:
        p.temp_alarm_hi = (int16_t)val;
        break;
    case 2:
        p.temp_alarm_lo = (int16_t)val;
        break;
    case 3:
        if (val < 2500 || val > 5000)
            return false;
        p.batt_alarm_mv = val;
        break;
    case 4:
        if (val > 4)
            return false;
        p.baud_code = (uint8_t)val;
        break;
    case 5:
        p.mute = (uint8_t)(val ? 1 : 0);
        break;
    case 7:
        p.sleep_en = (uint8_t)(val ? 1 : 0);
        break;
    default:
        return false;
    }
    app_set_params(&p);

    /* 波特率变更需同步重配 USART2：响应仍按新波特率发出，
       上位机写完波特率码后须切到新波特率等响应（多数主机工具如此约定） */
    if (reg == 4)
        (void)bsp_485_set_baud_code(p.baud_code);
    return true;
}

void comm_task(void *arg)
{
    (void)arg;
    static const modbus_cfg_t mb_cfg = {
        .read_input = mb_read_input,
        .read_holding = mb_read_holding,
        .write_holding = mb_write_holding,
    };

    /* 初始游标预取最新一条 */
    prefetch_cursor(0);

    uint8_t req[RX_FRAME_MAX];
    uint8_t resp[TX_FRAME_MAX];

    for (;;)
    {
        if (xSemaphoreTake(bsp_485_frame_sem(), pdMS_TO_TICKS(1000)) == pdTRUE)
        {
            uint16_t n = rb_read(bsp_485_rxbuf(), req, sizeof(req));
            uint16_t off = 0;
            /* 一次唤醒可能已缓冲多帧（主机连发），逐帧解析，避免只处理首帧而丢后续 */
            while (n > 0 && off < n)
            {
                uint16_t fl = modbus_frame_len(&req[off], (uint16_t)(n - off));
                if (fl == 0)
                    break; /* 剩余字节不足一帧或已损坏，丢弃 */

                device_params_t p;
                app_get_params(&p);
                modbus_cfg_t cfg = mb_cfg;
                cfg.slave_addr = p.modbus_addr;

                uint16_t rn = modbus_process_frame(&cfg, &req[off], fl, resp, sizeof(resp));
                if (rn > 0)
                    bsp_485_send(resp, rn, 100);
                off += fl;
            }
        }
        bsp_wdg_task_feed(WDG_TASK_COMM);
    }
}
