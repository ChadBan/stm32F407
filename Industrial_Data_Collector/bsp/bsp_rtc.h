/**
 * @file    bsp_rtc.h
 * @brief   RTC 实时时钟：读写日期时间，供采集数据打时间戳
 */
#ifndef BSP_RTC_H
#define BSP_RTC_H

#include "board_config.h"
#include "util_time.h"

void bsp_rtc_init(void);

bool bsp_rtc_get(dt_t *out);
bool bsp_rtc_set(const dt_t *in);

#endif /* BSP_RTC_H */
