/**
 * @file    bsp_uart.c
 * @brief   调试串口 + RS485 DMA/IDLE 实现
 */
#include "bsp_uart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* ============ 调试串口 ============ */
void bsp_dbg_printf(const char *fmt, ...)
{
    char tmp[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if (n > (int)sizeof(tmp))
        n = sizeof(tmp);
    HAL_UART_Transmit(&huart_dbg, (uint8_t *)tmp, (uint16_t)n, 100);
}

/* Keil MDK 下重定向 fputc 到调试串口，使 printf 生效 */
int fputc(int ch, FILE *f)
{
    uint8_t c = (uint8_t)ch;
    HAL_UART_Transmit(&huart_dbg, &c, 1, HAL_MAX_DELAY);
    (void)f;
    return ch;
}

/* ============ RS485 ============ */
#define RS485_DMA_RX_SIZE 256
#define RS485_RB_SIZE 512 /* 2 的幂 */

static uint8_t g_rs485_dma_rx[RS485_DMA_RX_SIZE];
static uint8_t g_rs485_rb_mem[RS485_RB_SIZE];
static ringbuf_t g_rs485_rb;
static SemaphoreHandle_t g_rs485_frame_sem;

bool bsp_485_init(void)
{
    if (!rb_init(&g_rs485_rb, g_rs485_rb_mem, RS485_RB_SIZE))
        return false;
    if (g_rs485_frame_sem == NULL)
    {
        g_rs485_frame_sem = xSemaphoreCreateBinary();
        if (g_rs485_frame_sem == NULL)
            return false;
    }
    RS485_SET_RX(); /* 默认接收方向 */
    /* 启动 DMA 循环接收 + IDLE 中断，收到空闲即触发 RxEventCallback */
    return HAL_UARTEx_ReceiveToIdle_DMA(&huart_485, g_rs485_dma_rx,
                                        RS485_DMA_RX_SIZE) == HAL_OK;
}

ringbuf_t *bsp_485_rxbuf(void) { return &g_rs485_rb; }
SemaphoreHandle_t bsp_485_frame_sem(void) { return g_rs485_frame_sem; }

/* DMA/IDLE 事件回调：size 为本次 IDLE 前收到的字节数。
   DMA 工作于 NORMAL 模式，一帧结束后 HAL 已停止接收，必须在此重新挂接。 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != huart_485.Instance)
        return;
    BaseType_t hi_wake = pdFALSE;
    if (size > 0)
    {
        rb_write(&g_rs485_rb, g_rs485_dma_rx, size);
        xSemaphoreGiveFromISR(g_rs485_frame_sem, &hi_wake);
    }
    /* 重新挂载 DMA+IDLE 接收，准备下一帧 */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart_485, g_rs485_dma_rx, RS485_DMA_RX_SIZE);
    if (hi_wake)
        portYIELD_FROM_ISR(hi_wake);
}

/* 接收错误(溢出/噪声/帧错误)后必须重启接收，否则 DMA 永久卡死 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != huart_485.Instance)
        return;
    HAL_UART_DMAStop(&huart_485);
    rb_reset(&g_rs485_rb);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart_485, g_rs485_dma_rx, RS485_DMA_RX_SIZE);
}

bool bsp_485_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (data == NULL || len == 0)
        return false;
    RS485_SET_TX();
    HAL_StatusTypeDef st = HAL_UART_Transmit(&huart_485, (uint8_t *)data, len, timeout_ms);
    /* 等待移位寄存器发完最后字节再切回 RX，否则末字节被截断。
       用 HAL_GetTick 计时：纯寄存器自旋递减太快，微秒内即退出导致提前切向 */
    uint32_t t0 = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(&huart_485, UART_FLAG_TC) == RESET)
    {
        if ((HAL_GetTick() - t0) > timeout_ms)
            break;
    }
    RS485_SET_RX();
    return st == HAL_OK;
}

/* 与 device_params_t.baud_code 的约定一致：0..4 -> 9600..115200 */
static const uint32_t BAUD_TABLE[5] = {9600, 19200, 38400, 57600, 115200};

bool bsp_485_set_baud_code(uint8_t code)
{
    if (code > 4)
        return false;
    /* 停掉当前接收，重配 BRR 后重挂 DMA+IDLE 接收 */
    HAL_UART_AbortReceive(&huart_485);
    huart_485.Init.BaudRate = BAUD_TABLE[code];
    if (HAL_UART_Init(&huart_485) != HAL_OK)
        return false;
    return bsp_485_init();
}
