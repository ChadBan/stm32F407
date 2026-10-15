/**
 * @file    app_types.h
 * @brief   应用层共享数据结构（任务间传递、参数、寄存器映射约定）
 */
#ifndef APP_TYPES_H
#define APP_TYPES_H

#include "board_config.h"
#include "util_time.h"

/* 一次采集结果（带时间戳），经队列在任务间传递 */
typedef struct
{
    dt_t ts;                 /* 采集时刻 */
    int32_t temp_x1000;      /* 温度 x1000 (C) */
    uint32_t humi_x1000;     /* 湿度 x1000 (%) */
    uint32_t battery_mv;     /* 电池电压 mV */
    uint16_t sensor1_raw;    /* 模拟传感器1 原始值 */
    uint16_t sensor2_raw;    /* 模拟传感器2 原始值 */
    uint16_t current_mv;     /* 4-20mA 回路经采样电阻后的电压 mV */
    uint8_t valid_bits;      /* bit0:temp bit1:humi bit2:batt ... 有效标志 */
} collect_sample_t;

/* 可掉电保存的设备参数（写入 W25Q） */
typedef struct
{
    uint8_t modbus_addr;    /* RS485 从机地址 1..247 */
    int16_t temp_alarm_hi;  /* 温度报警上限 x10 (0.1C) */
    int16_t temp_alarm_lo;  /* 温度报警下限 x10 */
    uint16_t batt_alarm_mv; /* 电池欠压报警阈值 mV */
    uint8_t baud_code;      /* 0:9600 1:19200 2:38400 3:57600 4:115200 */
    uint8_t mute;           /* 报警消音 */
    uint8_t sleep_en;       /* 低功耗 STOP 使能（空闲超时后休眠） */
} device_params_t;

/* 显示页面 */
typedef enum
{
    PAGE_MAIN = 0, /* 主界面：温湿度/电压 */
    PAGE_TIME,     /* 时间/日期 */
    PAGE_PARAM,    /* 参数设置 */
    PAGE_STATUS,   /* 状态/报警 */
    PAGE_COUNT
} disp_page_t;

/* 报警位定义 */
#define ALM_BIT_TEMP_HI (1u << 0)
#define ALM_BIT_TEMP_LO (1u << 1)
#define ALM_BIT_BATT_LO (1u << 2)
#define ALM_BIT_SENSOR (1u << 3)

#define PARAM_VALID_BITS_TEMP 0x01
#define PARAM_VALID_BITS_HUMI 0x02
#define PARAM_VALID_BITS_BATT 0x04
#define VALID_BITS_SENSOR 0x08
#define VALID_BITS_TIME 0x10

#endif /* APP_TYPES_H */
