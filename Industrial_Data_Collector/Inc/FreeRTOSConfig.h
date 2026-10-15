/**
 * @file    FreeRTOSConfig.h
 * @brief   FreeRTOS v10.x 配置（STM32F407 / Cortex-M4F）
 *
 * 关键点：
 *  - 中断优先级分组 NVIC_PRIORITYGROUP_4，HAL 使用的优先级必须 >= configMAX_SYSCALL_INTERRUPT_PRIORITY
 *  - configCHECK_FOR_STACK_OVERFLOW / configUSE_MALLOC_FAILED_HOOK 用于栈与堆监控
 *  - configUSE_IDLE_HOOK 开启以支持 STOP 低功耗
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "stm32f4xx.h"
#include <stdio.h>

#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_IDLE_HOOK 1
#define configUSE_TICK_HOOK 0
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ ((TickType_t)1000) /* 1ms 节拍 */
#define configMAX_PRIORITIES 7
#define configMINIMAL_STACK_SIZE ((uint16_t)128)
#define configTOTAL_HEAP_SIZE ((size_t)(16 * 1024))
#define configMAX_TASK_NAME_LEN 16
#define configUSE_16_BIT_TICKS 0
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 256

/* 同步原语：互斥锁(bsp_i2c/spi、app_state 使用，含优先级继承) */
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0

/* 钩子 / 监控 */
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_APPLICATION_TASK_TAG 0

/* 运行期统计（栈监控/任务列表） */
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1
#define configGENERATE_RUN_TIME_STATS 0

/* 阻塞 API 使能 */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler
/* SysTick 由 stm32f4xx_it.c 中的 SysTick_Handler 分发：先喂 HAL 时基
   (HAL_IncTick)，再调 FreeRTOS 的 tick，保证 HAL_Delay/HAL_GetTick 可用 */
#define xPortSysTickHandler SysTick_Handler_FreeRTOS

#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_xResumeFromISR 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTimerPendFunctionCall 1
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xEventGroupSetBitFromISR 1
#define INCLUDE_pcTaskGetTaskName 1

/* 中断优先级：F407 4 位优先级，数值越小越高。
   可安全调用 FromISR API 的中断优先级门限。 */
#define configPRIO_BITS 4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0x0F
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 0x05
#define configKERNEL_INTERRUPT_PRIORITY (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configASSERT(x)           \
    if ((x) == 0)                 \
    {                             \
        taskDISABLE_INTERRUPTS(); \
        for (;;)                  \
            ;                     \
    }

#endif /* FREERTOS_CONFIG_H */
