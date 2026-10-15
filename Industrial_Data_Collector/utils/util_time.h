/**
 * @file    util_time.h
 * @brief   RTC 时间戳工具：日期时间 <-> 秒级时间戳，格式化
 */
#ifndef UTIL_TIME_H
#define UTIL_TIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* 简单日期时间结构，避免依赖 RTC_TimeTypeDef/DateTypeDef 于上层 */
typedef struct
{
    uint16_t year;  /* 2000..2099 */
    uint8_t month;  /* 1..12 */
    uint8_t day;    /* 1..31 */
    uint8_t hour;   /* 0..23 */
    uint8_t minute; /* 0..59 */
    uint8_t second; /* 0..59 */
} dt_t;

/* 合法性校验 */
bool dt_is_valid(const dt_t *dt);

/* 转为相对 2000-01-01 00:00:00 的秒时间戳（用于日志排序，非 UTC 严格历法） */
uint32_t dt_to_epoch(const dt_t *dt);
void epoch_to_dt(uint32_t epoch, dt_t *dt);

/* 格式化为 "YYYY-MM-DD HH:MM:SS"，out 至少 20 字节，返回写入长度 */
size_t dt_format(const dt_t *dt, char *out, size_t cap);

#endif /* UTIL_TIME_H */
