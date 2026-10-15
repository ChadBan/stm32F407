/**
 * @file    dev_modbus.h
 * @brief   Modbus-RTU 从机协议栈（与传输层/业务解耦，通过回调读写寄存器）
 *
 * 支持功能码：0x03 读保持、0x04 读输入、0x06 写单个保持、0x10 写多个保持。
 * 设备不直接依赖 app：寄存器读写通过注册回调完成，异常按规范回送异常码。
 */
#ifndef DEV_MODBUS_H
#define DEV_MODBUS_H

#include "board_config.h"

/* 异常码 */
#define MB_EX_ILLEGAL_FUNCTION 0x01
#define MB_EX_ILLEGAL_ADDR 0x02
#define MB_EX_ILLEGAL_VALUE 0x03

/* 寄存器读写回调：成功返回 true 并填值；地址非法返回 false */
typedef bool (*mb_read_cb_t)(uint16_t reg, uint16_t *out);
typedef bool (*mb_write_cb_t)(uint16_t reg, uint16_t val);

typedef struct
{
    uint8_t slave_addr;          /* 本机从机地址 */
    mb_read_cb_t read_input;     /* 0x04 输入寄存器(只读遥测) */
    mb_read_cb_t read_holding;   /* 0x03 保持寄存器 */
    mb_write_cb_t write_holding; /* 0x06/0x10 写保持 */
} modbus_cfg_t;

/* 处理一帧请求，生成响应帧；返回响应长度（0 表示丢弃，如地址不匹配/广播） */
uint16_t modbus_process_frame(const modbus_cfg_t *cfg,
                              const uint8_t *req, uint16_t req_len,
                              uint8_t *resp, uint16_t resp_cap);

/* 从 buf 起始推断一帧完整长度（含 CRC），供字节流解析多帧用；
   返回 0 表示字节不足一帧或 CRC 校验失败（调用方应丢弃剩余数据） */
uint16_t modbus_frame_len(const uint8_t *buf, uint16_t len);

#endif /* DEV_MODBUS_H */
