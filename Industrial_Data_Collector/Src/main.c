/**
 * @file    main.c
 * @brief   应用入口：初始化 -> 创建任务 -> 启动调度器；含空闲/错误钩子
 *
 * CubeMX 生成的 SystemClock_Config 与各外设 MX_*_Init 在此调用。
 * 低功耗：空闲钩子在“仅剩 idle 任务可运行”时进入 STOP，按键 EXTI 唤醒。
 */
#include "board_config.h"
#include "FreeRTOS.h"
#include "task.h"

#include "bsp_gpio.h"
#include "bsp_adc.h"
#include "bsp_i2c.h"
#include "bsp_spi.h"
#include "bsp_uart.h"
#include "bsp_rtc.h"
#include "bsp_pwm.h"
#include "bsp_wdg.h"

#include "dev_oled.h"
#include "app_state.h"
#include "app_tasks.h"

/* --- CubeMX 生成 --- */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_ADC1_Init(void);
extern void MX_I2C1_Init(void);
extern void MX_SPI1_Init(void);
extern void MX_USART1_UART_Init(void);
extern void MX_USART2_UART_Init(void);
extern void MX_TIM4_Init(void);
extern void MX_RTC_Init(void);
extern void MX_DMA_Init(void);

static void bsp_init_all(void)
{
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_I2C1_Init();
    MX_SPI1_Init();
    MX_USART1_UART_Init();
    MX_USART2_UART_Init();
    MX_ADC1_Init();
    MX_TIM4_Init();
    MX_RTC_Init();

    bsp_gpio_init();
    (void)bsp_i2c_init();
    (void)bsp_spi_init();
    (void)bsp_485_init();
    bsp_rtc_init();
    bsp_pwm_init();
    (void)bsp_adc_init();
    bsp_wdg_init(3000); /* IWDG ~3s */
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    bsp_init_all();

    /* 业务初始化：状态中心依赖 SPI Flash 读参数，须在驱动 init 之后 */
    (void)app_state_init();
    (void)oled_init();

    /* 创建任务，失败则进入错误处理（此处靠 IWDG 复位兜底） */
    (void)app_tasks_start();

    vTaskStartScheduler();
    for (;;)
    {
    } /* 调度器不应返回 */
}

/* ============ FreeRTOS 钩子 ============ */

/* 空闲钩子：满足条件才进入 STOP。唤醒源：RTC 唤醒定时器(1s) + 按键 WKUP(PA0/EXTI0)。
   IWDG 在 STOP 下仍由 LSI 运行且无法暂停，故休眠期间每 1s 唤醒一次强制喂狗，
   唤醒间隔必须远小于 IWDG 超时(main 中配为 3s)。 */
#define SLEEP_IDLE_MS 30000u /* 无按键/通讯活动 30s 后允许休眠 */

static bool sleep_allowed(void)
{
    device_params_t p;
    app_get_params(&p);
    return p.sleep_en && app_get_alarm_bits() == 0 &&
           app_ms_since_activity() > SLEEP_IDLE_MS;
}

void vApplicationIdleHook(void)
{
    if (!sleep_allowed())
    {
        /* 不休眠：轻度等待中断，等待下一个事件(仍受 SysTick 1ms 唤醒) */
        __WFI();
        return;
    }

    HAL_SuspendTick();
    /* 1s 周期唤醒：AsynchPrediv=127,SynchPrediv=255 -> 1Hz; 装 0 => 下一秒唤醒 */
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 0, RTC_WAKEUPCLOCK_CK_SPRE_17BITS);
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* 唤醒后时钟回 HSI，须重新配置系统时钟 */
    SystemClock_Config();
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    HAL_ResumeTick();

    /* 恢复周期性采集/接收外设（STOP 期间 DMA 被停） */
    (void)bsp_adc_init();
    (void)bsp_485_init();
    /* 强制喂狗并刷新心跳（其余任务休眠期间被冻结属正常） */
    bsp_wdg_force_refresh();
}

/* 内存分配失败钩子：栈/堆耗尽，停喂狗让 IWDG 复位，同时点亮 LED 报警 */
void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
        bsp_led_toggle();
    }
}

/* 栈溢出钩子（configCHECK_FOR_STACK_OVERFLOW 开启时生效） */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}
