/**
 * @file    bsp_i2c.h
 * @brief   I2C1 主模式线程安全封装（互斥锁 + 超时）
 *
 * 多个任务(采集读 SHT30、显示刷 OLED)共享同一条 I2C 总线，必须加互斥锁，
 * 且 HAL_I2C_xxx 使用带超时版本，避免总线被从机拉死导致任务永久阻塞。
 */
#ifndef BSP_I2C_H
#define BSP_I2C_H

#include "board_config.h"

bool bsp_i2c_init(void);

/* 探测从机是否存在：发 START+地址+STOP 看是否 ACK（不写数据，安全）。
   dev_addr 为 7bit 地址（HAL 内部自动左移 1 位），返回 true 表示从机应答。 */
bool bsp_i2c_probe(uint16_t dev_addr);

/* 写 len 字节到从机，dev_addr 为 7bit 地址（如 SHT30=0x44） */
bool bsp_i2c_write(uint16_t dev_addr, const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/* 读 len 字节 */
bool bsp_i2c_read(uint16_t dev_addr, uint8_t *data, uint16_t len, uint32_t timeout_ms);

/* 先写寄存器地址再重复起始读数据（SHT30/常用传感器时序） */
bool bsp_i2c_mem_read(uint16_t dev_addr, const uint8_t *reg, uint16_t reg_len,
                      uint8_t *data, uint16_t len, uint32_t timeout_ms);

#endif /* BSP_I2C_H */
