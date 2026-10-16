/**
 * @file    stm32f4xx_it.c
 * @brief   中断服务程序实现
 */
#include "board_config.h"
#include "stm32f4xx_it.h"
#include "bsp_uart.h"

/* 外部句柄（定义于 board_periph.c） */
extern DMA_HandleTypeDef hdma_usart2_rx;
extern UART_HandleTypeDef huart_485;

/* ============ Cortex-M 内核异常 ============ */
void NMI_Handler(void)
{
    for (;;)
    {
    }
}
void HardFault_Handler(void)
{ /* 打印标记后主动复位；复位原因打印中会显示 soft */
    bsp_dbg_printf("!! fault: hard\r\n");
    NVIC_SystemReset();
}
void MemManage_Handler(void)
{
    bsp_dbg_printf("!! fault: memmanage\r\n");
    NVIC_SystemReset();
}
void BusFault_Handler(void)
{
    bsp_dbg_printf("!! fault: bus\r\n");
    NVIC_SystemReset();
}
void UsageFault_Handler(void)
{
    bsp_dbg_printf("!! fault: usage\r\n");
    NVIC_SystemReset();
}
void DebugMon_Handler(void) {}

/* SysTick：先喂 HAL 时基（HAL_GetTick/HAL_Delay 依赖），再进 FreeRTOS tick。
   注意优先级固定为最低(15)，不受 FreeRTOS 中断优先级门限影响。
   关键：调度器启动前禁止进 FreeRTOS tick——pxDelayedTaskList 等内核链表
   尚未初始化(NULL)，提前调 xTaskIncrementTick 会解引用 NULL/垃圾指针
   触发 HardFault，uwTick 冻结导致 HAL_Delay 死循环。 */
extern void SysTick_Handler_FreeRTOS(void);
static volatile uint8_t g_rtos_started = 0;
void sys_tick_rtos_enable(void)
{
    g_rtos_started = 1;
}
void SysTick_Handler(void)
{
    HAL_IncTick();
    if (g_rtos_started != 0)
    {
        SysTick_Handler_FreeRTOS();
    }
}

/* ============ 按键 EXTI ============ */
void EXTI0_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
void EXTI3_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3); }
void EXTI4_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_4); }

/* ============ RS485：USART2 事件 + DMA RX ============ */
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart_485);
}
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

/* ============ ADC DMA ============ */
void DMA2_Stream0_IRQHandler(void)
{
    extern DMA_HandleTypeDef hdma_adc1;
    HAL_DMA_IRQHandler(&hdma_adc1);
}

/* ============ RTC 唤醒（低功耗周期唤醒喂狗） ============ */
void RTC_WKUP_IRQHandler(void)
{
    HAL_RTCEx_WakeUpTimerIRQHandler(&hrtc);
}
