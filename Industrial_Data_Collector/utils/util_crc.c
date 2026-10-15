/**
 * @file    util_crc.c
 * @brief   CRC16/Modbus 实现
 */
#include "util_crc.h"

uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    if (data == NULL || len == 0)
    {
        return crc;
    }
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x0001)
            {
                crc = (uint16_t)((crc >> 1) ^ 0xA001);
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

size_t crc16_modbus_append(uint8_t *frame, size_t payload_len, size_t frame_cap)
{
    if (frame == NULL || payload_len + 2 > frame_cap)
    {
        return 0;
    }
    uint16_t crc = crc16_modbus(frame, payload_len);
    frame[payload_len] = (uint8_t)(crc & 0xFF); /* 低字节在前 */
    frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    return payload_len + 2;
}

bool crc16_modbus_check(const uint8_t *frame, size_t frame_len)
{
    if (frame == NULL || frame_len < 3)
    {
        return false;
    }
    uint16_t calc = crc16_modbus(frame, frame_len - 2);
    uint16_t recv = (uint16_t)(frame[frame_len - 2] | (frame[frame_len - 1] << 8));
    return calc == recv;
}
