/**
 * @file    dev_lcd.h
 * @brief   ILI9488 3.5寸 TFT LCD（FSMC 8080 16bit 并口）驱动接口
 *
 * 探索者板载 3.5 寸屏：480x320，ILI9488 驱动 IC，FSMC Bank1 NE4。
 * FSMC 底层初始化见 board_periph.c 的 MX_FSMC_Init；本文件负责
 * ILI9488 的寄存器初始化序列与像素绘制。
 */
#ifndef DEV_LCD_H
#define DEV_LCD_H

#include "board_config.h"

#define LCD_WIDTH  480
#define LCD_HEIGHT 320

/* 每字符 8x16，横向排布时每字符占 8 像素宽 */
#define LCD_CHAR_W 8
#define LCD_CHAR_H 16

/* RGB565 颜色 */
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410

void lcd_init(void);   /* 复位 + ILI9488 初始化序列 + 清屏 + 开背光 */
void lcd_clear(uint16_t color);
void lcd_fill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);
void lcd_show_char(uint16_t x, uint16_t y, char ch, uint16_t color, uint16_t bg);
void lcd_show_string(uint16_t x, uint16_t y, const char *s, uint16_t color, uint16_t bg);

#endif /* DEV_LCD_H */
