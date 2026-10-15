/**
 * @file    util_time.c
 * @brief   时间戳工具实现
 */
#include "util_time.h"

static const uint8_t MDAYS[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool is_leap(uint16_t y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint8_t days_in_month(uint16_t y, uint8_t m)
{
    if (m == 2 && is_leap(y))
        return 29;
    return (m >= 1 && m <= 12) ? MDAYS[m - 1] : 0;
}

bool dt_is_valid(const dt_t *dt)
{
    if (dt == NULL)
        return false;
    if (dt->year < 2000 || dt->year > 2099)
        return false;
    if (dt->month < 1 || dt->month > 12)
        return false;
    if (dt->day < 1 || dt->day > days_in_month(dt->year, dt->month))
        return false;
    if (dt->hour > 23 || dt->minute > 59 || dt->second > 59)
        return false;
    return true;
}

uint32_t dt_to_epoch(const dt_t *dt)
{
    if (!dt_is_valid(dt))
        return 0;
    /* 从 2000-01-01 起累计天数 */
    uint32_t days = 0;
    for (uint16_t y = 2000; y < dt->year; y++)
    {
        days += is_leap(y) ? 366 : 365;
    }
    for (uint8_t m = 1; m < dt->month; m++)
    {
        days += days_in_month(dt->year, m);
    }
    days += (uint32_t)(dt->day - 1);
    return days * 86400u + dt->hour * 3600u + dt->minute * 60u + dt->second;
}

void epoch_to_dt(uint32_t epoch, dt_t *dt)
{
    if (dt == NULL)
        return;
    uint32_t days = epoch / 86400u;
    uint32_t rem = epoch % 86400u;
    dt->hour = (uint8_t)(rem / 3600u);
    dt->minute = (uint8_t)((rem % 3600u) / 60u);
    dt->second = (uint8_t)(rem % 60u);

    uint16_t y = 2000;
    while (1)
    {
        uint32_t dy = is_leap(y) ? 366 : 365;
        if (days < dy)
            break;
        days -= dy;
        y++;
        if (y > 2099)
        {
            y = 2099;
            break;
        }
    }
    dt->year = y;
    uint8_t m = 1;
    while (m <= 12)
    {
        uint8_t dm = days_in_month(y, m);
        if (days < dm)
            break;
        days -= dm;
        m++;
    }
    dt->month = m;
    dt->day = (uint8_t)(days + 1);
}

/* 简易无符号转十进制，返回写入长度 */
static size_t u32_to_str(uint32_t v, char *buf, uint8_t min_width)
{
    char tmp[10];
    uint8_t n = 0;
    if (v == 0)
    {
        tmp[n++] = '0';
    }
    while (v > 0 && n < sizeof(tmp))
    {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n < min_width)
        tmp[n++] = '0';
    for (uint8_t k = 0; k < n; k++)
        buf[k] = tmp[n - 1 - k];
    buf[n] = '\0';
    return n;
}

size_t dt_format(const dt_t *dt, char *out, size_t cap)
{
    if (dt == NULL || out == NULL || cap < 20)
        return 0;
    size_t p = 0;
    p += u32_to_str(dt->year, out + p, 4);
    out[p++] = '-';
    p += u32_to_str(dt->month, out + p, 2);
    out[p++] = '-';
    p += u32_to_str(dt->day, out + p, 2);
    out[p++] = ' ';
    p += u32_to_str(dt->hour, out + p, 2);
    out[p++] = ':';
    p += u32_to_str(dt->minute, out + p, 2);
    out[p++] = ':';
    p += u32_to_str(dt->second, out + p, 2);
    out[p] = '\0';
    return p;
}
