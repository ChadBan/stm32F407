/**
 * @file    bsp_rtc.c
 * @brief   RTC 实现（HAL，格式 24h，二进制计数）
 */
#include "bsp_rtc.h"

/* F407 RTC 年份字段为 0-199（相对 2000） */
#define RTC_BASE_YEAR 2000

void bsp_rtc_init(void)
{
    RTC_DateTypeDef d;
    RTC_TimeTypeDef t;
    /* 若掉电后 RTC 丢失秒/日期（备份域未供电），设一个初值避免 1970 */
    if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != 0xA55A)
    {
        d.Year = 26; /* 2026 */
        d.Month = 1;
        d.Date = 1;
        d.WeekDay = RTC_WEEKDAY_MONDAY;
        t.Hours = 0;
        t.Minutes = 0;
        t.Seconds = 0;
        t.SubSeconds = 0;
        t.TimeFormat = RTC_HOURFORMAT_24;
        t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);
        HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0xA55A);
    }
}

bool bsp_rtc_get(dt_t *out)
{
    if (out == NULL)
        return false;
    RTC_DateTypeDef d;
    RTC_TimeTypeDef t;
    if (HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK)
        return false;
    /* GetDate 必须紧随 GetTime 调用以锁存，否则日期不准 */
    if (HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK)
        return false;
    out->year = (uint16_t)(RTC_BASE_YEAR + d.Year);
    out->month = d.Month;
    out->day = d.Date;
    out->hour = t.Hours;
    out->minute = t.Minutes;
    out->second = t.Seconds;
    return dt_is_valid(out);
}

bool bsp_rtc_set(const dt_t *in)
{
    if (!dt_is_valid(in))
        return false;
    RTC_DateTypeDef d = {0};
    RTC_TimeTypeDef t = {0};
    d.Year = (uint8_t)(in->year - RTC_BASE_YEAR);
    d.Month = in->month;
    d.Date = in->day;
    d.WeekDay = RTC_WEEKDAY_MONDAY;
    t.Hours = in->hour;
    t.Minutes = in->minute;
    t.Seconds = in->second;
    t.SubSeconds = 0;
    t.TimeFormat = RTC_HOURFORMAT_24;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    if (HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK)
        return false;
    return HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) == HAL_OK;
}
