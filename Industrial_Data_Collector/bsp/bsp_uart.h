/**
 * @file    bsp_uart.h
 * @brief   调试串口(printf) + RS485 DMA+IDLE 帧接收
 *
 * RS485 采用 DMA 循环接收 + IDLE 线中断判定一帧结束（Modbus 3.5T 静默即帧间隔），
 * 收到帧后通过二值信号量唤醒通讯任务，全程非轮询。
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include "board_config.h"
#include "util_ringbuf.h"
#include "semphr.h"

/* 调试串口：重定向 printf */
void bsp_dbg_printf(const char *fmt, ...);

/* --- RS485 --- */
bool bsp_485_init(void);

/* 取到最近一帧接收数据的可读接口（通讯任务用） */
ringbuf_t *bsp_485_rxbuf(void);
SemaphoreHandle_t bsp_485_frame_sem(void); /* 每收到一帧 give 一次 */

/* 发送一帧（自动 DE 方向切换 + 等待发送完成） */
bool bsp_485_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/* 按参数码(0:9600 1:19200 2:38400 3:57600 4:115200)重配波特率并重挂 DMA 接收 */
bool bsp_485_set_baud_code(uint8_t code);

#endif /* BSP_UART_H */
