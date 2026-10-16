/**
 * @file    board_config.h
 * @brief   板级资源集中配置（唯一硬件映射来源）
 *
 * 工程约定：所有引脚、外设句柄、FreeRTOS 任务参数都集中在此文件，
 * 业务/驱动代码不得出现硬编码引脚号。更换硬件只需修改本文件与 CubeMX。
 *
 * 芯片：STM32F407ZGT6   RTOS：FreeRTOS   HAL：STM32CubeMX 生成
 *
 * 说明：低层外设初始化（时钟、GPIO、ADC、SPI、I2C、USART、TIM、RTC、DMA）
 * 由 CubeMX 生成于 Src/stm32f4xx_hal_msp.c 及各 xxx_Init()，本文件用 extern
 * 声明这些句柄，供 bsp/device/app 层使用。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * CubeMX 生成的外设句柄（外部声明）
 * ============================================================ */
extern ADC_HandleTypeDef hadc1;      /* ADC1：软件触发 + DMA 循环采样多通道 */
extern I2C_HandleTypeDef hi2c_1;     /* I2C1：SHT30 + OLED(SSD1306)         */
extern SPI_HandleTypeDef hspi_1;     /* SPI1：W25Q128 Flash                 */
extern UART_HandleTypeDef huart_dbg; /* USART1：调试串口 (printf)           */
extern UART_HandleTypeDef huart_485; /* USART2：RS485 (Modbus RTU 从机)     */
extern TIM_HandleTypeDef htim_pwm;   /* TIM4 CH1：蜂鸣器 PWM                */
extern RTC_HandleTypeDef hrtc;       /* RTC：日历 + 唤醒                    */

/* ============================================================
 * GPIO：指示灯 / 蜂鸣器使能 / RS485 方向 / 按键
 * ============================================================ */
/* LED 运行指示灯（推挽输出，低电平点亮） */
#define LED_RUN_PORT GPIOC
#define LED_RUN_PIN GPIO_PIN_13

/* RS485 收发方向控制（探索者标准：SP3485 的 DE/RE 并联，经跳线帽 P7 接 PG9，
   默认低电平=接收。若你的板子方向脚在别处，只改这里即可） */
#define RS485_DE_PORT GPIOG
#define RS485_DE_PIN GPIO_PIN_9
#define RS485_SET_TX() HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET)
#define RS485_SET_RX() HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET)

/* 蜂鸣器（无源，由 TIM4-CH1 PWM 驱动，PD12 复用，见 board_periph.c） */
#define BUZZER_TIM_CH TIM_CHANNEL_1
#define BUZZER_PWM_PERIOD_HZ 2000 /* 无源蜂鸣器共振频率，按实际器件调整 */

/* 按键（探索者标准，3 路：确认/上/下）
   WK_UP(PA0)：下拉，按下接 3.3V → 高电平有效；兼 STOP 唤醒（F407 仅 PA0 可 WKUP）
   KEY1(PE3)/KEY0(PE4)：上拉，按下接地 → 低电平有效
   active_high 区分两种极性，bsp_key_pressed 据此归一化返回“是否按下”。 */
typedef enum
{
    KEY_OK = 0, /* PA0   EXTI0 / WK_UP */
    KEY_UP,     /* PE3   EXTI3 / KEY1  */
    KEY_DOWN,   /* PE4   EXTI4 / KEY0  */
    KEY_ID_MAX
} key_id_t;

/* 按键引脚映射（下标与 key_id_t 对应）*/
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    bool active_high; /* true=按下高电平(WK_UP)，false=按下低电平(KEY0/1) */
} key_pin_t;
#define KEY_PIN_TABLE {                             \
    {GPIOA, GPIO_PIN_0, true }, /* KEY_OK   高有效 */ \
    {GPIOE, GPIO_PIN_3, false}, /* KEY_UP   低有效 */ \
    {GPIOE, GPIO_PIN_4, false}, /* KEY_DOWN 低有效 */ \
}

/* 电池电压采集分压比：R1/R2 组成的分压，ADC 输入 = Vbat * R2/(R1+R2) */
#define BATTERY_DIVIDER_X100 50u /* 0.50，即 1/2 分压，放大 100 倍存储 */

/* ============================================================
 * ADC 通道映射（与 hadc1 规则组顺序、DMA 缓冲区索引一致）
 * ============================================================ */
typedef enum
{
    ADC_CH_BATTERY = 0, /* 电池电压（分压后） */
    ADC_CH_SENSOR1,     /* 外部模拟传感器 1   */
    ADC_CH_SENSOR2,     /* 外部模拟传感器 2   */
    ADC_CH_CURRENT,     /* 4-20mA 电流采样    */
    ADC_CH_COUNT
} adc_channel_t;

/* W25Q128 SPI Flash 片选（软件控制，推挽输出，低有效）
   探索者板载 W25Q128 接 SPI1：PB3=SCK PB4=MISO PB5=MOSI PB14=CS */
#define W25Q_CS_PORT GPIOB
#define W25Q_CS_PIN GPIO_PIN_14
#define W25Q_CS_LOW() HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_RESET)
#define W25Q_CS_HIGH() HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_SET)

/* 3.5寸 TFT LCD（ILI9488 480x320）FSMC 8080 16bit 并口（探索者板载）
   数据线 D0-D15、RD/WR/CS/RS 由 FSMC 硬件固定映射（见 board_periph.c 的
   MX_FSMC_Init），此处只定义两个普通 GPIO：
   RST=PB15（LCD 复位）、BL=PB0（背光，经 S8050 三极管，高电平点亮）。 */
#define LCD_RST_PORT GPIOB
#define LCD_RST_PIN  GPIO_PIN_15
#define LCD_BL_PORT  GPIOB
#define LCD_BL_PIN   GPIO_PIN_0
#define LCD_RST_LOW()  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET)
#define LCD_RST_HIGH() HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET)
#define LCD_BL_ON()    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_SET)
#define LCD_BL_OFF()   HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, GPIO_PIN_RESET)

/* ADC 参考电压(mV)与满量程(12bit) */
#define ADC_VREF_MV 3300
#define ADC_FULLSCALE 4095

/* ============================================================
 * 采样/上报周期（ms）
 * ============================================================ */
#define COLLECT_PERIOD_MS 500
#define DISPLAY_PERIOD_MS 200
#define COMM_TIMEOUT_MS 100 /* Modbus 帧间超时(3.5T 放大) */

/* ============================================================
 * FreeRTOS 任务：栈(词字)、优先级、周期
 * 数值越大优先级越高（configMAX_PRIORITIES 之内）。
 * 规划：IDLE(0) < DISPLAY(2) < COLLECT(3) < COMM(4) < ALARM(5) < KEY(6)
 * ============================================================ */
#define PRIO_IDLE 0
#define PRIO_DISPLAY 2
#define PRIO_COLLECT 3
#define PRIO_COMM 4
#define PRIO_ALARM 5
#define PRIO_KEY 6

#define TASK_PRIO_COLLECT PRIO_COLLECT
#define TASK_PRIO_COMM PRIO_COMM
#define TASK_PRIO_KEY PRIO_KEY /* 按键响应最快 */
#define TASK_PRIO_ALARM PRIO_ALARM
#define TASK_PRIO_DISPLAY PRIO_DISPLAY

#define TASK_STACK_COLLECT 256
/* comm 栈内 req/resp 各 256B，写参数路径再嵌套 w25q 页缓冲 256B，需 2KB */
#define TASK_STACK_COMM 512
#define TASK_STACK_KEY 128
#define TASK_STACK_ALARM 128
#define TASK_STACK_DISPLAY 256

/* 队列深度 */
#define QUEUE_DEPTH_DATA 8 /* 采集->显示/上报 数据队列 */

#endif /* BOARD_CONFIG_H */
