/**
 * @file    dev_sht30.c
 * @brief   SHT30 驱动实现
 */
#include "dev_sht30.h"
#include "bsp_i2c.h"

#define SHT30_ADDR 0x88          /* 8bit 写地址（ADDR->LOW 时 0x44<<1） */
#define SHT30_CMD_MEAS_HS 0x2C06 /* single-shot, clock stretch, high repeatability */

/* SHT30 CRC-8：多项式 0x31，初值 0xFF */
static uint8_t sht30_crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
        {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool sht30_read(sht30_sample_t *out)
{
    if (out == NULL)
        return false;
    out->valid = false;

    /* 发送测量命令（2 字节，大端） */
    uint8_t cmd[2] = {(uint8_t)(SHT30_CMD_MEAS_HS >> 8), (uint8_t)(SHT30_CMD_MEAS_HS & 0xFF)};
    if (!bsp_i2c_write(SHT30_ADDR, cmd, 2, 20))
        return false;

    /* 读 6 字节：Tmsb Tlsb Tcrc Hmsb Hlsb Hcrc（clock stretching 会阻塞等待） */
    uint8_t raw[6];
    if (!bsp_i2c_read(SHT30_ADDR, raw, 6, 50))
        return false;

    if (sht30_crc8(&raw[0], 2) != raw[2] || sht30_crc8(&raw[3], 2) != raw[5])
    {
        return false; /* CRC 校验失败，数据丢弃 */
    }

    uint16_t t_raw = (uint16_t)((raw[0] << 8) | raw[1]);
    uint16_t h_raw = (uint16_t)((raw[3] << 8) | raw[4]);

    /* T = -45 + 175 * SrT / 65535；x1000 定点，int64 防中间值溢出(int32 装不下) */
    int64_t t_num = (int64_t)t_raw * 175000 - 45LL * 65536 * 1000;
    out->temp_x1000 = (int32_t)(t_num / 65536);
    /* RH = 100 * SrH / 65535；同样用 uint64 防溢出 */
    out->humi_x1000 = (uint32_t)((uint64_t)h_raw * 100000u / 65536u);
    out->valid = true;
    return true;
}
