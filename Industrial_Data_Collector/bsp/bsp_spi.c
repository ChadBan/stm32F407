/**
 * @file    bsp_spi.c
 * @brief   SPI1 封装实现
 */
#include "bsp_spi.h"
#include "semphr.h"
#include <string.h>

static SemaphoreHandle_t g_spi_mutex;

bool bsp_spi_init(void)
{
    if (g_spi_mutex == NULL)
    {
        g_spi_mutex = xSemaphoreCreateMutex();
        if (g_spi_mutex == NULL)
            return false;
    }
    W25Q_CS_HIGH();
    return true;
}

bool bsp_spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len, uint32_t timeout_ms)
{
    if (len == 0 || (tx == NULL && rx == NULL))
        return false;
    if (g_spi_mutex == NULL)
        return false;
    if (xSemaphoreTake(g_spi_mutex, pdMS_TO_TICKS(timeout_ms + 50)) != pdTRUE)
        return false;

    W25Q_CS_LOW();
    HAL_StatusTypeDef st;
    if (tx != NULL && rx != NULL)
    {
        st = HAL_SPI_TransmitReceive(&hspi_1, (uint8_t *)tx, rx, len, timeout_ms);
    }
    else if (tx != NULL)
    {
        st = HAL_SPI_Transmit(&hspi_1, (uint8_t *)tx, len, timeout_ms);
    }
    else
    {
        static const uint8_t dummy[1] = {0xFF};
        st = HAL_SPI_TransmitReceive(&hspi_1, (uint8_t *)dummy, rx, len, timeout_ms);
    }
    W25Q_CS_HIGH();

    xSemaphoreGive(g_spi_mutex);
    return st == HAL_OK;
}

uint8_t bsp_spi_rw_byte(uint8_t byte, uint32_t timeout_ms)
{
    uint8_t rx = 0;
    bsp_spi_xfer(&byte, &rx, 1, timeout_ms);
    return rx;
}

bool bsp_spi_xfer_flash(const uint8_t *hdr, uint16_t hdr_len,
                        const uint8_t *tx, uint8_t *rx, uint32_t len,
                        uint32_t timeout_ms)
{
    if (hdr == NULL || hdr_len == 0)
        return false;
    if (g_spi_mutex == NULL)
        return false;
    if (xSemaphoreTake(g_spi_mutex, pdMS_TO_TICKS(timeout_ms + 50)) != pdTRUE)
        return false;

    /* CS 全程拉低：W25Q 命令在 CS 上升沿才结束，中间拉高会丢弃命令 */
    W25Q_CS_LOW();
    HAL_StatusTypeDef st = HAL_SPI_Transmit(&hspi_1, (uint8_t *)hdr, hdr_len, timeout_ms);
    if (st == HAL_OK)
    {
        if (tx != NULL && rx != NULL)
        {
            st = HAL_SPI_TransmitReceive(&hspi_1, (uint8_t *)tx, rx, (uint16_t)len, timeout_ms);
        }
        else if (tx != NULL)
        {
            st = HAL_SPI_Transmit(&hspi_1, (uint8_t *)tx, (uint16_t)len, timeout_ms);
        }
        else if (rx != NULL)
        {
            /* 只读：发 dummy 时钟换数据，分块避免大缓冲 */
            uint8_t dummy[64];
            memset(dummy, 0xFF, sizeof(dummy));
            uint32_t got = 0;
            while (st == HAL_OK && got < len)
            {
                uint32_t n = (len - got < sizeof(dummy)) ? (len - got) : sizeof(dummy);
                st = HAL_SPI_TransmitReceive(&hspi_1, dummy, rx + got, (uint16_t)n, timeout_ms);
                got += n;
            }
        }
    }
    W25Q_CS_HIGH();

    xSemaphoreGive(g_spi_mutex);
    return st == HAL_OK;
}
