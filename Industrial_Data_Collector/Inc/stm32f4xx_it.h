/**
 * @file    stm32f4xx_it.h
 * @brief   中断处理函数声明
 */
#ifndef STM32F4XX_IT_H
#define STM32F4XX_IT_H

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void DebugMon_Handler(void);

/* 外设中断（FreeRTOS 的 SVC/PendSV/SysTick 由 FreeRTOSConfig 宏映射到 port.c）*/
void EXTI0_IRQHandler(void);
void EXTI3_IRQHandler(void);
void EXTI4_IRQHandler(void);
void USART2_IRQHandler(void);
void DMA1_Stream5_IRQHandler(void);
void DMA2_Stream0_IRQHandler(void);
void RTC_WKUP_IRQHandler(void);

#endif /* STM32F4XX_IT_H */
