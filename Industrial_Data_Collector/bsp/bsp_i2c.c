/**
 * @file    bsp_i2c.c
 * @brief   I2C 线程安全封装实现
 */
#include "bsp_i2c.h"
#include "bsp_uart.h"
#include "semphr.h"
#include "task.h"

/* 1=打印每次 I2C 失败/recover 细节。SHT30 未接时 collect 任务每周期都会
   NACK 失败，默认关掉避免刷屏；排查总线问题时再临时打开。 */
#define I2C_DEBUG 0

static SemaphoreHandle_t g_i2c_mutex;

bool bsp_i2c_init(void)
{
    if (g_i2c_mutex == NULL)
    {
        /* 互斥锁（带优先级继承），防止显示/采集任务优先级反转 */
        g_i2c_mutex = xSemaphoreCreateMutex();
        if (g_i2c_mutex == NULL)
            return false;
    }
    return true;
}

static bool i2c_lock(uint32_t timeout_ms)
{
    if (g_i2c_mutex == NULL)
        return false;
    /* 调度器未启动时是单线程，无需互斥；且此时调用 xSemaphoreTake 会进入/退出
       临界区，FreeRTOS 哨兵计数 uxCriticalNesting=0xaaaaaaaa 使 BASEPRI 残留
       0x50（屏蔽 SysTick），HAL_GetTick 冻结，HAL_I2C 超时死循环，最终被 IWDG
       复位。故启动阶段直接放行，保证启动路径上的 I2C 探测/OLED 初始化不卡死。 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
        return true;
    return xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(timeout_ms + 50)) == pdTRUE;
}

static void i2c_unlock(void)
{
    if (g_i2c_mutex == NULL)
        return;
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED)
        return;
    xSemaphoreGive(g_i2c_mutex);
}

/* 该 HAL 版本 Master_Transmit/Receive 的错误路径(NACK/超时)不复位句柄
   hState，一次失败后所有 I2C 操作永久 HAL_BUSY(OLED 会因此彻底不刷新)。
   失败后 DeInit+Init 重建外设，恢复总线与状态。须在 mutex 保护内调用。 */
static void i2c_recover(void)
{
    /* 先软件打 9 个 SCL 脉冲 + 生成 STOP，把把持 SDA 的从机(时钟拉伸/半字节
       卡死)释放出来。依赖上拉才能看到高电平：无上拉时此步骤同样无效。 */
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7; /* SCL PB6 / SDA PB7 */
    g.Mode = GPIO_MODE_OUTPUT_OD;
    g.Pull = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET); /* 释放 SDA */
    for (int i = 0; i < 9; i++)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    }
    /* 生成 STOP：SCL 高期间 SDA 由低变高 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);

    HAL_I2C_DeInit(&hi2c_1);
    HAL_I2C_Init(&hi2c_1);
#if I2C_DEBUG
    bsp_dbg_printf("[i2c] recover\r\n");
#endif
}

static void i2c_report(const char *op, HAL_StatusTypeDef st, uint16_t dev_addr)
{
#if I2C_DEBUG
    bsp_dbg_printf("[%s] st=%u dev=%x state=%x ec=%x\r\n", op,
                   (unsigned)st, dev_addr,
                   (unsigned)hi2c_1.State, (unsigned)hi2c_1.ErrorCode);
#else
    (void)op; (void)st; (void)dev_addr;
#endif
}

bool bsp_i2c_probe(uint16_t dev_addr)
{
    if (!i2c_lock(50))
        return false;
    /* 只发 START + 地址 + STOP，判断从机是否 ACK；不写任何数据字节，安全 */
    HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(&hi2c_1, dev_addr, 2, 50);
    i2c_unlock();
    return st == HAL_OK;
}

bool bsp_i2c_write(uint16_t dev_addr, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0)
        return false;
    if (!i2c_lock(timeout_ms))
        return false;
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c_1, dev_addr, (uint8_t *)data, len, timeout_ms);
    if (st != HAL_OK)
        i2c_recover();
    i2c_unlock();
    if (st != HAL_OK)
        i2c_report("tx", st, dev_addr);
    return st == HAL_OK;
}

bool bsp_i2c_read(uint16_t dev_addr, uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0)
        return false;
    if (!i2c_lock(timeout_ms))
        return false;
    HAL_StatusTypeDef st = HAL_I2C_Master_Receive(&hi2c_1, dev_addr, data, len, timeout_ms);
    if (st != HAL_OK)
        i2c_recover();
    i2c_unlock();
    if (st != HAL_OK)
        i2c_report("rx", st, dev_addr);
    return st == HAL_OK;
}

bool bsp_i2c_mem_read(uint16_t dev_addr, const uint8_t *reg, uint16_t reg_len,
                      uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (reg == NULL || data == NULL || len == 0)
        return false;
    if (!i2c_lock(timeout_ms))
        return false;
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(&hi2c_1, dev_addr,
                                            reg[0], I2C_MEMADD_SIZE_8BIT,
                                            data, len, timeout_ms);
    (void)reg_len; /* 当前仅支持 1 字节寄存器地址；如需 2 字节扩展此处 */
    if (st != HAL_OK)
        i2c_recover();
    i2c_unlock();
    if (st != HAL_OK)
        i2c_report("mem", st, dev_addr);
    return st == HAL_OK;
}
