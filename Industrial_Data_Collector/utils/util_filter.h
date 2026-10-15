/**
 * @file    util_filter.h
 * @brief   采样软件滤波：滑动均值 + 中值 + 一阶低通
 *
 * ADC/传感器原始值噪声大，工业采集常见组合：先去极值均值 + 中值抑制脉冲。
 */
#ifndef UTIL_FILTER_H
#define UTIL_FILTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FILTER_WIN_MAX 16

typedef struct
{
    int32_t window[FILTER_WIN_MAX];
    uint8_t win; /* 有效窗口长度 */
    uint8_t cnt; /* 已填充样本数 */
    uint8_t idx; /* 环形写入下标 */
    int32_t sum; /* 窗口和（增量维护） */
} moving_avg_t;

void ma_init(moving_avg_t *f, uint8_t win);
int32_t ma_push(moving_avg_t *f, int32_t sample); /* 放入样本并返回当前均值 */

/* N 点中值滤波：拷贝到临时数组排序取中值，抑制毛刺。n<=FILTER_WIN_MAX */
int32_t median_filter(const int32_t *samples, size_t n);

/* 去极值均值：去掉最大最小各一个后求均值（n>=3 有效） */
int32_t trimmed_mean(const int32_t *samples, size_t n);

/* 一阶低通（指数加权）：y = a*x + (1-a)*y_prev，a 为定点(0..1024)/1024 */
int32_t lowpass1(int32_t prev, int32_t sample, uint16_t alpha_x1024);

#endif /* UTIL_FILTER_H */
