/**
 * @file    bsp_adc.c
 * @brief   ADC + DMA 循环采样实现
 */
#include "bsp_adc.h"

/* DMA 目标缓冲区：循环模式，长度 = 通道数 × 每通道扫描次数 */
#define ADC_SCAN_TIMES 4
static volatile uint16_t g_adc_dma_buf[ADC_CH_COUNT * ADC_SCAN_TIMES];

bool bsp_adc_init(void)
{
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_dma_buf,
                          ADC_CH_COUNT * ADC_SCAN_TIMES) != HAL_OK)
    {
        return false;
    }
    return true;
}

void bsp_adc_snapshot(uint16_t out[ADC_CH_COUNT])
{
    if (out == NULL)
        return;
    /* 每个通道取多次扫描的平均，抑制瞬时噪声 */
    for (int ch = 0; ch < ADC_CH_COUNT; ch++)
    {
        uint32_t sum = 0;
        for (int k = 0; k < ADC_SCAN_TIMES; k++)
        {
            sum += g_adc_dma_buf[k * ADC_CH_COUNT + ch];
        }
        out[ch] = (uint16_t)(sum / ADC_SCAN_TIMES);
    }
}

uint32_t bsp_adc_raw_to_mv(uint16_t raw)
{
    if (raw > ADC_FULLSCALE)
        raw = ADC_FULLSCALE;
    return (uint32_t)raw * ADC_VREF_MV / ADC_FULLSCALE;
}

uint32_t bsp_adc_battery_mv(void)
{
    uint16_t snap[ADC_CH_COUNT];
    bsp_adc_snapshot(snap);
    uint32_t pin_mv = bsp_adc_raw_to_mv(snap[ADC_CH_BATTERY]);
    /* 还原分压：Vbat = Vpin * 100 / BATTERY_DIVIDER_X100 */
    return pin_mv * 100u / BATTERY_DIVIDER_X100;
}
