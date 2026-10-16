/**
 * @file    dev_modbus.c
 * @brief   Modbus-RTU 从机协议实现
 */
#include "dev_modbus.h"
#include "util_crc.h"
#include <string.h>

#define FC_READ_HOLDING 0x03
#define FC_READ_INPUT 0x04
#define FC_WRITE_SINGLE 0x06
#define FC_WRITE_MULTI 0x10

#define MB_MAX_REGS 125 /* 单帧最多读 125 个寄存器 */

static uint16_t put_exception(uint8_t *resp, uint8_t addr, uint8_t fc, uint8_t ex)
{
    resp[0] = addr;
    resp[1] = fc | 0x80;
    resp[2] = ex;
    return (uint16_t)crc16_modbus_append(resp, 3, 8);
}

uint16_t modbus_frame_len(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len < 4)
        return 0;
    uint16_t need;
    switch (buf[1])
    {
    case FC_READ_HOLDING:
    case FC_READ_INPUT:
    case FC_WRITE_SINGLE:
        need = 8;
        break;
    case FC_WRITE_MULTI:
        if (len < 7)
            return 0;
        need = (uint16_t)(9 + buf[6]);
        break;
    default:
        /* 未知功能码按最小请求帧处理，交给协议栈回异常 */
        need = 8;
        break;
    }
    if (need > len || !crc16_modbus_check(buf, need))
        return 0;
    return need;
}

uint16_t modbus_process_frame(const modbus_cfg_t *cfg,
                              const uint8_t *req, uint16_t req_len,
                              uint8_t *resp, uint16_t resp_cap)
{
    if (cfg == NULL || req == NULL || resp == NULL || req_len < 4)
        return 0;
    if (!crc16_modbus_check(req, req_len))
        return 0; /* CRC 错，丢弃不回(规范) */

    uint8_t addr = req[0];
    uint8_t fc = req[1];
    bool broadcast = (addr == 0x00);
    if (!broadcast && addr != cfg->slave_addr)
        return 0; /* 非本机地址，忽略 */

    switch (fc)
    {
    case FC_READ_HOLDING:
    case FC_READ_INPUT:
    {
        /* 广播(addr=0)仅限写操作，读请求不应有响应（Modbus 规范） */
        if (broadcast)
            return 0;
        if (req_len != 8)
            return 0;
        uint16_t reg = (req[2] << 8) | req[3];
        uint16_t qty = (req[4] << 8) | req[5];
        if (qty == 0 || qty > MB_MAX_REGS)
        {
            return put_exception(resp, addr, fc, MB_EX_ILLEGAL_VALUE);
        }
        mb_read_cb_t cb = (fc == FC_READ_INPUT) ? cfg->read_input : cfg->read_holding;
        if (cb == NULL)
            return put_exception(resp, addr, fc, MB_EX_ILLEGAL_FUNCTION);

        uint16_t byte_cnt = (uint16_t)(qty * 2);
        if ((uint32_t)(3 + byte_cnt + 2) > resp_cap)
            return 0;
        resp[0] = addr;
        resp[1] = fc;
        resp[2] = (uint8_t)byte_cnt;
        for (uint16_t i = 0; i < qty; i++)
        {
            uint16_t v = 0;
            if (!cb((uint16_t)(reg + i), &v))
            {
                return put_exception(resp, addr, fc, MB_EX_ILLEGAL_ADDR);
            }
            resp[3 + i * 2] = (uint8_t)(v >> 8);
            resp[3 + i * 2 + 1] = (uint8_t)(v & 0xFF);
        }
        return (uint16_t)crc16_modbus_append(resp, (size_t)(3 + byte_cnt), resp_cap);
    }

    case FC_WRITE_SINGLE:
    {
        if (req_len != 8)
            return 0;
        uint16_t reg = (req[2] << 8) | req[3];
        uint16_t val = (req[4] << 8) | req[5];
        if (cfg->write_holding == NULL)
            return put_exception(resp, addr, fc, MB_EX_ILLEGAL_FUNCTION);
        if (!cfg->write_holding(reg, val))
            return put_exception(resp, addr, fc, MB_EX_ILLEGAL_ADDR);
        if (broadcast)
            return 0; /* 广播写无响应 */
        /* 回显请求(去掉 CRC 重算) */
        memcpy(resp, req, 6);
        return (uint16_t)crc16_modbus_append(resp, 6, resp_cap);
    }

    case FC_WRITE_MULTI:
    {
        /* addr fc regH regL qtyH qtyL byteCnt [data...] crc */
        if (req_len < 9)
            return 0;
        uint16_t reg = (req[2] << 8) | req[3];
        uint16_t qty = (req[4] << 8) | req[5];
        uint8_t bcnt = req[6];
        if (qty == 0 || qty > MB_MAX_REGS || bcnt != qty * 2 ||
            req_len < (uint16_t)(9 + bcnt))
        {
            return put_exception(resp, addr, fc, MB_EX_ILLEGAL_VALUE);
        }
        if (cfg->write_holding == NULL)
            return put_exception(resp, addr, fc, MB_EX_ILLEGAL_FUNCTION);
        for (uint16_t i = 0; i < qty; i++)
        {
            uint16_t val = (req[7 + i * 2] << 8) | req[7 + i * 2 + 1];
            if (!cfg->write_holding((uint16_t)(reg + i), val))
                return put_exception(resp, addr, fc, MB_EX_ILLEGAL_ADDR);
        }
        if (broadcast)
            return 0;
        resp[0] = addr;
        resp[1] = fc;
        resp[2] = (uint8_t)(reg >> 8);
        resp[3] = (uint8_t)reg;
        resp[4] = (uint8_t)(qty >> 8);
        resp[5] = (uint8_t)qty;
        return (uint16_t)crc16_modbus_append(resp, 6, resp_cap);
    }

    default:
        return put_exception(resp, addr, fc, MB_EX_ILLEGAL_FUNCTION);
    }
}
