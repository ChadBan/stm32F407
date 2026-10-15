/**
 * @file    stm32f4xx_it.c
 * @brief   中断服务程序实现
 */
#include "board_config.h"
#include "stm32f4xx_it.h"

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
{ /* 生产：记录现场后触发复位 */
    for (;;)
    {
    }
}
void MemManage_Handler(void)
{
    for (;;)
    {
    }
}
void BusFault_Handler(void)
{
    for (;;)
    {
    }
}
void UsageFault_Handler(void)
{
    for (;;)
    {
    }
}
void DebugMon_Handler(void) {}

/* SysTick：先喂 HAL 时基（HAL_GetTick/HAL_Delay 依赖），再进 FreeRTOS tick。
   注意优先级固定为最低(15)，不受 FreeRTOS 中断优先级门限影响。 */
extern void SysTick_Handler_FreeRTOS(void);
void SysTick_Handler(void)
{
    HAL_IncTick();
    SysTick_Handler_FreeRTOS();
}

/* ============ 按键 EXTI ============ */
void EXTI0_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_0); }
void EXTI2_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_2); }
void EXTI3_IRQHandler(void) { HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_3); }

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
