/**
 * @file    dev_oled.c
 * @brief   SSD1306 帧缓冲驱动实现
 */
#include "dev_oled.h"
#include "bsp_i2c.h"
#include <string.h>

#define OLED_ADDR 0x78 /* 8bit 写地址 (0x3C<<1) */
#define OLED_PAGES (OLED_HEIGHT / 8)
#define CTRL_CMD 0x00  /* Co=0, D/C=0 -> 命令字节流 */
#define CTRL_DATA 0x40 /* Co=0, D/C=1 -> 数据 */

static uint8_t g_fb[OLED_PAGES][OLED_WIDTH]; /* 帧缓冲：bit=1 点亮 */

/* ---- I2C 原语 ---- */
static bool oled_cmds(const uint8_t *seq, uint8_t n)
{
    /* 命令前缀字节 + 一串命令 */
    uint8_t buf[2 + 26];
    if ((size_t)n + 1 > sizeof(buf))
        return false;
    buf[0] = CTRL_CMD;
    memcpy(&buf[1], seq, n);
    return bsp_i2c_write(OLED_ADDR, buf, (uint16_t)(n + 1), 30);
}

bool oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,       // display off
        0x20, 0x00, // horizontal addressing mode
        0xB0,       // page0 start
        0xC8,       // COM scan remapped
        0x00, 0x10, // lower/upper column start
        0x40,       // start line 0
        0x81, 0xCF, // contrast
        0xA1,       // segment remap
        0xA6,       // normal display
        0xA8, 0x3F, // multiplex ratio 64
        0xA4,       // resume from RAM
        0xD3, 0x00, // display offset 0
        0xD5, 0x80, // clock divide
        0xD9, 0xF1, // pre-charge
        0xDA, 0x12, // COM pins
        0xDB, 0x40, // Vcomh
        0x8D, 0x14, // charge pump enable
        0xAF        // display on
    };
    if (!oled_cmds(init_seq, sizeof(init_seq)))
        return false;
    oled_clear();
    return oled_flush();
}

void oled_clear(void)
{
    memset(g_fb, 0, sizeof(g_fb));
}

bool oled_flush(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++)
    {
        uint8_t set[] = {(uint8_t)(0xB0 + page), 0x00, 0x10};
        if (!oled_cmds(set, sizeof(set)))
            return false;
        /* 数据块：CTRL_DATA + 128 像素字节 */
        uint8_t buf[1 + OLED_WIDTH];
        buf[0] = CTRL_DATA;
        memcpy(&buf[1], g_fb[page], OLED_WIDTH);
        if (!bsp_i2c_write(OLED_ADDR, buf, sizeof(buf), 50))
            return false;
    }
    return true;
}

static void fb_set(uint8_t x, uint8_t y, bool on)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
        return;
    if (on)
        g_fb[y / 8][x] |= (uint8_t)(1u << (y % 8));
    else
        g_fb[y / 8][x] &= (uint8_t)~(1u << (y % 8));
}

void oled_hline(uint8_t x, uint8_t y, uint8_t w)
{
    for (uint8_t i = 0; i < w; i++)
        fb_set((uint8_t)(x + i), y, true);
}

/* ---- 5x7 ASCII 字体（列式，bit0=最上行）---- */
typedef struct
{
    char ch;
    uint8_t col[5];
} glyph_t;

static const glyph_t FONT[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'!', {0x00, 0x00, 0x5F, 0x00, 0x00}},
    {'-', {0x04, 0x04, 0x04, 0x04, 0x04}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x10, 0x38, 0x6C, 0x86, 0x00}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'%', {0x23, 0x13, 0x08, 0x64, 0x62}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x41, 0x51, 0x32}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x30}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static const uint8_t *glyph_for(char ch)
{
    if (ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 'a' + 'A'); /* 小写降级 */
    for (uint8_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); i++)
    {
        if (FONT[i].ch == ch)
            return FONT[i].col;
    }
    return NULL;
}

void oled_putc(uint8_t x, uint8_t y, char ch)
{
    const uint8_t *g = glyph_for(ch);
    if (g == NULL)
    {
        /* 未收录字形：画空心 5x7 边框 */
        for (uint8_t col = 0; col < 5; col++)
        {
            for (uint8_t row = 0; row < 7; row++)
            {
                bool edge = (col == 0 || col == 4 || row == 0 || row == 6);
                fb_set((uint8_t)(x + col), (uint8_t)(y + row), edge);
            }
        }
        return;
    }
    for (uint8_t col = 0; col < 5; col++)
    {
        uint8_t bits = g[col];
        for (uint8_t row = 0; row < 7; row++)
        {
            fb_set((uint8_t)(x + col), (uint8_t)(y + row), (bits >> row) & 0x01);
        }
    }
}

uint8_t oled_puts(uint8_t x, uint8_t y, const char *s)
{
    if (s == NULL)
        return x;
    while (*s && x + 6 <= OLED_WIDTH)
    {
        oled_putc(x, y, *s++);
        x = (uint8_t)(x + 6); /* 5 列 + 1 间隔 */
    }
    return x;
}
