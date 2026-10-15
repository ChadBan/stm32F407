/**
 * @file    bsp_i2c.c
 * @brief   I2C 线程安全封装实现
 */
#include "bsp_i2c.h"
#include "semphr.h"

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
    return xSemaphoreTake(g_i2c_mutex, pdMS_TO_TICKS(timeout_ms + 50)) == pdTRUE;
}

static void i2c_unlock(void)
{
    xSemaphoreGive(g_i2c_mutex);
}

bool bsp_i2c_write(uint16_t dev_addr, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0)
        return false;
    if (!i2c_lock(timeout_ms))
        return false;
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(&hi2c_1, dev_addr, (uint8_t *)data, len, timeout_ms);
    i2c_unlock();
    return st == HAL_OK;
}

bool bsp_i2c_read(uint16_t dev_addr, uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0)
        return false;
    if (!i2c_lock(timeout_ms))
        return false;
    HAL_StatusTypeDef st = HAL_I2C_Master_Receive(&hi2c_1, dev_addr, data, len, timeout_ms);
    i2c_unlock();
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
    i2c_unlock();
    return st == HAL_OK;
}
