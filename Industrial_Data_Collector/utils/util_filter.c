/**
 * @file    util_filter.c
 * @brief   采样滤波实现（详见 util_filter.h）
 */
#include "util_filter.h"

void ma_init(moving_avg_t *f, uint8_t win)
{
    if (f == NULL)
        return;
    if (win == 0 || win > FILTER_WIN_MAX)
        win = FILTER_WIN_MAX;
    f->win = win;
    f->cnt = 0;
    f->idx = 0;
    f->sum = 0;
}

int32_t ma_push(moving_avg_t *f, int32_t sample)
{
    if (f == NULL || f->win == 0)
        return sample;
    if (f->cnt < f->win)
    {
        f->window[f->idx] = sample;
        f->sum += sample;
        f->cnt++;
    }
    else
    {
        /* 覆盖最旧样本，增量维护 sum */
        f->sum -= f->window[f->idx];
        f->window[f->idx] = sample;
        f->sum += sample;
    }
    f->idx = (uint8_t)((f->idx + 1) % f->win);
    return f->sum / (int32_t)f->cnt;
}

/* 简单插入排序（窗口很小，够用且无堆分配） */
static void sort_asc(int32_t *a, size_t n)
{
    for (size_t i = 1; i < n; i++)
    {
        int32_t key = a[i];
        size_t j = i;
        while (j > 0 && a[j - 1] > key)
        {
            a[j] = a[j - 1];
            j--;
        }
        a[j] = key;
    }
}

int32_t median_filter(const int32_t *samples, size_t n)
{
    if (samples == NULL || n == 0)
        return 0;
    if (n > FILTER_WIN_MAX)
        n = FILTER_WIN_MAX;
    int32_t tmp[FILTER_WIN_MAX];
    for (size_t i = 0; i < n; i++)
        tmp[i] = samples[i];
    sort_asc(tmp, n);
    return tmp[n / 2];
}

int32_t trimmed_mean(const int32_t *samples, size_t n)
{
    if (samples == NULL || n < 3)
    {
        int32_t s = 0;
        if (samples && n)
            for (size_t i = 0; i < n; i++)
                s += samples[i];
        return n ? s / (int32_t)n : 0;
    }
    if (n > FILTER_WIN_MAX)
        n = FILTER_WIN_MAX;
    int32_t tmp[FILTER_WIN_MAX];
    int32_t sum = 0;
    for (size_t i = 0; i < n; i++)
        tmp[i] = samples[i];
    sort_asc(tmp, n);
    /* 去掉首尾各一个 */
    for (size_t i = 1; i + 1 < n; i++)
        sum += tmp[i];
    return sum / (int32_t)(n - 2);
}

int32_t lowpass1(int32_t prev, int32_t sample, uint16_t alpha_x1024)
{
    if (alpha_x1024 > 1024)
        alpha_x1024 = 1024;
    int64_t acc = (int64_t)alpha_x1024 * sample + (int64_t)(1024 - alpha_x1024) * prev;
    return (int32_t)(acc / 1024);
}
