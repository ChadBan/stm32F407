/**
 * @file    util_crc.h
 * @brief   CRC16/Modbus 与通用校验工具
 */
#ifndef UTIL_CRC_H
#define UTIL_CRC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Modbus-RTU CRC16：多项式 0xA001(反向 0x8005)，初值 0xFFFF */
uint16_t crc16_modbus(const uint8_t *data, size_t len);

/* 追加 CRC（小端：低字节在前）到缓冲区末尾，返回新的总长度；空间不足返回 0 */
size_t crc16_modbus_append(uint8_t *frame, size_t payload_len, size_t frame_cap);

/* 校验整帧（含末尾 2 字节 CRC），正确返回 true */
bool crc16_modbus_check(const uint8_t *frame, size_t frame_len);

#endif /* UTIL_CRC_H */
