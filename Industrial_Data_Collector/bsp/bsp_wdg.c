/**
 * @file    bsp_wdg.c
 * @brief   看门狗监督实现
 */
#include "bsp_wdg.h"
#include "task.h"

static IWDG_HandleTypeDef g_hiwdg;
static volatile uint32_t g_heartbeat[WDG_TASK_NUM]; /* 各任务最近喂狗时刻(ms) */

void bsp_wdg_init(uint32_t timeout_ms)
{
    for (int i = 0; i < WDG_TASK_NUM; i++)
        g_heartbeat[i] = 0;

    /* F407 IWDG 时钟 = LSI ≈ 32kHz。预分频 64 -> 计数器 500Hz。
       重装值 = timeout_ms * 500 / 1000，最大 4095。 */
    g_hiwdg.Instance = IWDG;
    g_hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    g_hiwdg.Init.Reload = (uint16_t)(timeout_ms / 2u);
    if (g_hiwdg.Init.Reload > 0xFFF)
        g_hiwdg.Init.Reload = 0xFFF;
    HAL_IWDG_Init(&g_hiwdg);
}

void bsp_wdg_task_feed(wdg_task_id_t id)
{
    if (id >= WDG_TASK_NUM)
        return;
    g_heartbeat[id] = HAL_GetTick();
}

bool bsp_wdg_poll(uint32_t task_timeout_ms)
{
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < WDG_TASK_NUM; i++)
    {
        /* 无符号回绕安全的差值比较 */
        if ((uint32_t)(now - g_heartbeat[i]) > task_timeout_ms)
        {
            /* 有任务卡死：故意不喂狗，等待 IWDG 复位 */
            return false;
        }
    }
    HAL_IWDG_Refresh(&g_hiwdg);
    return true;
}

/* 强制喂狗：用于 STOP 休眠周期唤醒。同时刷新所有心跳，
   因为休眠期间其余任务被冻结属正常，不应被判为卡死。 */
void bsp_wdg_force_refresh(void)
{
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < WDG_TASK_NUM; i++)
        g_heartbeat[i] = now;
    HAL_IWDG_Refresh(&g_hiwdg);
}
