/**
 * @file    bsp_wdg.h
 * @brief   看门狗管理：IWDG 独立看门狗 + 任务心跳监督
 *
 * 思路：每个关键任务周期性调用 bsp_wdg_task_feed() 上报心跳；
 * 由一个监督点（可挂在最高优先级软定时器或通讯任务）调用 bsp_wdg_poll()：
 * 只有当所有被监管任务在超时窗口内都喂过快照时，才真正刷新 IWDG。
 * 任一任务卡死 -> 心跳缺失 -> 停止喂狗 -> IWDG 超时复位。
 */
#ifndef BSP_WDG_H
#define BSP_WDG_H

#include "board_config.h"

typedef enum
{
    WDG_TASK_COLLECT = 0,
    WDG_TASK_COMM,
    WDG_TASK_KEY,
    WDG_TASK_ALARM,
    WDG_TASK_DISPLAY,
    WDG_TASK_NUM
} wdg_task_id_t;

/* 启动 IWDG（超时由内部 LSI 与预分频决定，CubeMX 或此处配置） */
void bsp_wdg_init(uint32_t timeout_ms);

/* 任务心跳上报（写入单调时间戳） */
void bsp_wdg_task_feed(wdg_task_id_t id);

/* 监督：检查全部任务心跳是否健康，健康则喂 IWDG，返回是否健康 */
bool bsp_wdg_poll(uint32_t task_timeout_ms);

/* 强制喂狗（低功耗周期性唤醒时用，不经心跳校验） */
void bsp_wdg_force_refresh(void);

#endif /* BSP_WDG_H */
