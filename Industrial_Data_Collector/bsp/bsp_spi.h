/**
 * @file    bsp_spi.h
 * @brief   SPI1 全双工封装（W25Q Flash 用），片选软件控制 + 互斥锁
 */
#ifndef BSP_SPI_H
#define BSP_SPI_H

#include "board_config.h"

bool bsp_spi_init(void);

/* 一次完整传输（CS 拉低->收发->CS 拉高），线程安全 */
bool bsp_spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len, uint32_t timeout_ms);

/* NOR Flash 连续传输：CS 拉低后先发命令头 hdr，再发/收数据段，最后 CS 拉高。
   用于 W25Q 页编程/读数据等命令+数据必须在一段 CS 内完成的时序 */
bool bsp_spi_xfer_flash(const uint8_t *hdr, uint16_t hdr_len,
                        const uint8_t *tx, uint8_t *rx, uint32_t len,
                        uint32_t timeout_ms);

/* 发送单字节并接收回读字节 */
uint8_t bsp_spi_rw_byte(uint8_t byte, uint32_t timeout_ms);

#endif /* BSP_SPI_H */
