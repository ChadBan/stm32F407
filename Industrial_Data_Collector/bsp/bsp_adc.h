/**
 * @file    bsp_adc.h
 * @brief   ADC1 + DMA 循环多通道采样
 *
 * 采用 Scan + Continuous + DMA Circular：DMA 自动把各通道原始值搬到缓冲区，
 * CPU 不参与搬运（企业标准要求，禁止轮询 ADC）。任务只需读取快照。
 */
#ifndef BSP_ADC_H
#define BSP_ADC_H

#include "board_config.h"

/* 初始化 DMA 循环采样（CubeMX 已完成 hadc1/DMA 配置，此处启动） */
bool bsp_adc_init(void);

/* 取出当前各通道原始 12bit 值的快照（拷贝一份，避免读到半更新数据） */
void bsp_adc_snapshot(uint16_t out[ADC_CH_COUNT]);

/* 单通道原始值转毫伏 */
uint32_t bsp_adc_raw_to_mv(uint16_t raw);

/* 读取电池电压（mV），已按分压比还原 */
uint32_t bsp_adc_battery_mv(void);

#endif /* BSP_ADC_H */
