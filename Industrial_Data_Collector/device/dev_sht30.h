/**
 * @file    dev_sht30.h
 * @brief   SHT30-DIS 温湿度传感器设备驱动（I2C，含 CRC-8 校验）
 */
#ifndef DEV_SHT30_H
#define DEV_SHT30_H

#include "board_config.h"

/* 返回整数千分度：temp_x1000 = 温度*1000，如 25.12C -> 25120 */
typedef struct
{
    int32_t temp_x1000;
    uint32_t humi_x1000;
    bool valid;
} sht30_sample_t;

bool sht30_read(sht30_sample_t *out);

#endif /* DEV_SHT30_H */
