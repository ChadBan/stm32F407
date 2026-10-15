/**
 * @file    dev_oled.h
 * @brief   SSD1306 128x64 OLED（I2C），帧缓冲 + 8x16 文本
 *
 * 采用 1024 字节帧缓冲 + 整屏 flush：显示任务先写缓冲区再一次性刷新，
 * 减少 I2C 事务数量并避免画面撕裂。
 */
#ifndef DEV_OLED_H
#define DEV_OLED_H

#include "board_config.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

bool oled_init(void);
bool oled_flush(void); /* 把帧缓冲刷到面板 */
void oled_clear(void); /* 清帧缓冲（不立即刷） */

/* 在 (x,y) 显示一个 8x16 字符，x:0..127 y:0..63，仅置位帧缓冲 */
void oled_putc(uint8_t x, uint8_t y, char ch);
/* 显示字符串，超过宽度自动截断；返回下一可写 x */
uint8_t oled_puts(uint8_t x, uint8_t y, const char *s);
/* 画一条水平线（进度条/分隔线用） */
void oled_hline(uint8_t x, uint8_t y, uint8_t w);

#endif /* DEV_OLED_H */
