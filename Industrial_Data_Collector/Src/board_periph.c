/**
 * @file    board_periph.c
 * @brief   CubeMX 等价的底层外设初始化（本工程手写固化，非占位）
 *
 * 定义所有外设句柄、系统时钟(168MHz)、各 MX_*_Init 及 HAL MspInit 回调。
 * 与 Inc/board_config.h 的引脚/资源约定严格一致。
 */
#include "board_config.h"

/* ================= 外设句柄（board_config.h 中 extern 声明） ================= */
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
I2C_HandleTypeDef hi2c_1;
SPI_HandleTypeDef hspi_1;
UART_HandleTypeDef huart_dbg; /* USART1 */
UART_HandleTypeDef huart_485; /* USART2 */
DMA_HandleTypeDef hdma_usart2_rx;
TIM_HandleTypeDef htim_pwm; /* TIM4 */
RTC_HandleTypeDef hrtc;

/* ================= 系统时钟：HSE 8MHz -> 168MHz ================= */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState = RCC_HSE_ON;
    osc.LSIState = RCC_LSI_ON; /* IWDG/RTC 用 LSI */
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2; /* 168MHz */
    osc.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
    {
        for (;;)
        {
        }
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1; /* HCLK  168MHz */
    clk.APB1CLKDivider = RCC_HCLK_DIV4;  /* PCLK1  42MHz (TIM 84MHz) */
    clk.APB2CLKDivider = RCC_HCLK_DIV2;  /* PCLK2  84MHz (TIM 168MHz) */
    /* F407 无 OverDrive；Scale1 + FLASH_LATENCY_5 已达 168MHz */
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK)
    {
        for (;;)
        {
        }
    }
}

/* ================= GPIO / DMA / NVIC（板级基础） ================= */
void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* LED PC13 推挽输出，默认灭 */
    g.Pin = LED_RUN_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_RUN_PORT, &g);
    HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, GPIO_PIN_SET);

    /* RS485 DE PA1 推挽输出，默认接收 */
    g.Pin = RS485_DE_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RS485_DE_PORT, &g);
    RS485_SET_RX();

    /* W25Q CS PA8 推挽输出，默认高 */
    g.Pin = W25Q_CS_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(W25Q_CS_PORT, &g);
    W25Q_CS_HIGH();

    /* 按键：PA0/PA2/PA3 下拉输入 + 上升沿 EXTI；PC13 上拉输入 + 下降沿(低有效) */
    static const uint16_t keys_hi[3] = {GPIO_PIN_0, GPIO_PIN_2, GPIO_PIN_3};
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLDOWN;
    for (int i = 0; i < 3; i++)
    {
        g.Pin = keys_hi[i];
        HAL_GPIO_Init(GPIOA, &g);
    }

    HAL_NVIC_SetPriority(EXTI0_IRQn, 6, 0);
    HAL_NVIC_SetPriority(EXTI2_IRQn, 6, 0);
    HAL_NVIC_SetPriority(EXTI3_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(EXTI0_IRQn);
    HAL_NVIC_EnableIRQ(EXTI2_IRQn);
    HAL_NVIC_EnableIRQ(EXTI3_IRQn);
}

void MX_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE(); /* ADC1 -> DMA2 Stream0 */
    __HAL_RCC_DMA1_CLK_ENABLE(); /* USART2 RX -> DMA1 Stream5 */
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/* ================= ADC1：扫描 + 连续 + 软件触发 + DMA 循环 ================= */
void MX_ADC1_Init(void)
{
    ADC_ChannelConfTypeDef ch = {0};
    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    hadc1.Init.ScanConvMode = ENABLE;
    hadc1.Init.ContinuousConvMode = ENABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.NbrOfConversion = ADC_CH_COUNT;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
    HAL_ADC_Init(&hadc1);

    /* 4 个通道：电池/模拟1/模拟2/电流。ADC1_IN10..13 = PC0..PC3，
       与 HAL_ADC_MspInit 中 GPIOC 模拟引脚配置一致；
       PA0..PA3 已被按键/RS485 DE 占用，不可用于 ADC。 */
    const uint32_t chans[ADC_CH_COUNT] = {
        ADC_CHANNEL_10, ADC_CHANNEL_11, ADC_CHANNEL_12, ADC_CHANNEL_13};
    for (int i = 0; i < ADC_CH_COUNT; i++)
    {
        ch.Channel = chans[i];
        ch.Rank = i + 1;
        ch.SamplingTime = ADC_SAMPLETIME_56CYCLES;
        ch.Offset = 0;
        HAL_ADC_ConfigChannel(&hadc1, &ch);
    }
}

/* ADC 的 MSP + DMA 绑定 */
void HAL_ADC_MspInit(ADC_HandleTypeDef *a)
{
    GPIO_InitTypeDef g = {0};
    if (a->Instance == ADC1)
    {
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        /* PC0(PC1/PC2/PC3) = ADC1_IN10..IN13，模拟输入 */
        g.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
        g.Mode = GPIO_MODE_ANALOG;
        g.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOC, &g);

        hdma_adc1.Instance = DMA2_Stream0;
        hdma_adc1.Init.Channel = DMA_CHANNEL_1;
        hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
        hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        hdma_adc1.Init.Mode = DMA_CIRCULAR;
        hdma_adc1.Init.Priority = DMA_PRIORITY_MEDIUM;
        hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        HAL_DMA_Init(&hdma_adc1);
        __HAL_LINKDMA(a, DMA_Handle, hdma_adc1);
    }
}

/* ================= I2C1：400kHz ================= */
void MX_I2C1_Init(void)
{
    hi2c_1.Instance = I2C1;
    hi2c_1.Init.ClockSpeed = 400000;
    hi2c_1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c_1.Init.OwnAddress1 = 0;
    hi2c_1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c_1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c_1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c_1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c_1);
}
void HAL_I2C_MspInit(I2C_HandleTypeDef *i)
{
    GPIO_InitTypeDef g = {0};
    if (i->Instance == I2C1)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        g.Pin = GPIO_PIN_6 | GPIO_PIN_7; /* SCL PB6 / SDA PB7 */
        g.Mode = GPIO_MODE_AF_OD;
        g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF4_I2C1;
        HAL_GPIO_Init(GPIOB, &g);
        __HAL_RCC_I2C1_CLK_ENABLE();
    }
}

/* ================= SPI1：Mode0 <=21MHz ================= */
void MX_SPI1_Init(void)
{
    hspi_1.Instance = SPI1;
    hspi_1.Init.Mode = SPI_MODE_MASTER;
    hspi_1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi_1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi_1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi_1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi_1.Init.NSS = SPI_NSS_SOFT;
    hspi_1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4; /* 84/4=21MHz */
    hspi_1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    HAL_SPI_Init(&hspi_1);
}
void HAL_SPI_MspInit(SPI_HandleTypeDef *s)
{
    GPIO_InitTypeDef g = {0};
    if (s->Instance == SPI1)
    {
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7; /* SCK/MISO/MOSI */
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOA, &g);
        __HAL_RCC_SPI1_CLK_ENABLE();
    }
}

/* ================= USART1：调试 115200 ================= */
void MX_USART1_UART_Init(void)
{
    huart_dbg.Instance = USART1;
    huart_dbg.Init.BaudRate = 115200;
    huart_dbg.Init.WordLength = UART_WORDLENGTH_8B;
    huart_dbg.Init.StopBits = UART_STOPBITS_1;
    huart_dbg.Init.Parity = UART_PARITY_NONE;
    huart_dbg.Init.Mode = UART_MODE_TX_RX;
    huart_dbg.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_dbg.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart_dbg);
}

/* ================= USART2：RS485 115200 + DMA RX ================= */
void MX_USART2_UART_Init(void)
{
    huart_485.Instance = USART2;
    huart_485.Init.BaudRate = 115200;
    huart_485.Init.WordLength = UART_WORDLENGTH_8B;
    huart_485.Init.StopBits = UART_STOPBITS_1;
    huart_485.Init.Parity = UART_PARITY_NONE;
    huart_485.Init.Mode = UART_MODE_TX_RX;
    huart_485.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_485.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart_485);
}
void HAL_UART_MspInit(UART_HandleTypeDef *u)
{
    GPIO_InitTypeDef g = {0};
    if (u->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin = GPIO_PIN_9 | GPIO_PIN_10; /* TX/RX */
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &g);
    }
    else if (u->Instance == USART2)
    {
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        g.Pin = GPIO_PIN_5 | GPIO_PIN_6; /* TX=PD5 / RX=PD6, 避开 PA2/PA3 按键 */
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_PULLUP;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOD, &g);

        /* DMA1 Stream5 Channel4 -> USART2_RX */
        hdma_usart2_rx.Instance = DMA1_Stream5;
        hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4;
        hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
        hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
        hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
        hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
        hdma_usart2_rx.Init.Mode = DMA_NORMAL; /* IDLE 回调后重挂 */
        hdma_usart2_rx.Init.Priority = DMA_PRIORITY_HIGH;
        hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        HAL_DMA_Init(&hdma_usart2_rx);
        __HAL_LINKDMA(u, hdmarx, hdma_usart2_rx);

        HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
        HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
}

/* ================= TIM4-CH1：蜂鸣器 PWM ================= */
void MX_TIM4_Init(void)
{
    TIM_ClockConfigTypeDef c = {0};
    TIM_OC_InitTypeDef oc = {0};
    htim_pwm.Instance = TIM4;
    htim_pwm.Init.Prescaler = 100 - 1; /* 84M/100 = 840kHz */
    htim_pwm.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim_pwm.Init.Period = 420 - 1; /* 默认 ~2kHz */
    htim_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&htim_pwm);

    c.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    HAL_TIM_ConfigClockSource(&htim_pwm, &c);

    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    HAL_TIM_PWM_ConfigChannel(&htim_pwm, &oc, TIM_CHANNEL_1);
}
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *t)
{
    GPIO_InitTypeDef g = {0};
    if (t->Instance == TIM4)
    {
        __HAL_RCC_TIM4_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        g.Pin = GPIO_PIN_12; /* TIM4_CH1 -> PD12 */
        g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF2_TIM4;
        HAL_GPIO_Init(GPIOD, &g);
    }
}

/* ================= RTC：LSE 优先，退化 LSI ================= */
void MX_RTC_Init(void)
{
    RCC_PeriphCLKInitTypeDef pc = {0};
    pc.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pc.RTCClockSelection = RCC_RTCCLKSOURCE_LSI; /* 开发板无 LSE 时用 LSI */
    HAL_RCCEx_PeriphCLKConfig(&pc);
    __HAL_RCC_RTC_ENABLE();

    hrtc.Instance = RTC;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.AsynchPrediv = 127; /* LSI 32000Hz: /128 /256 */
    hrtc.Init.SynchPrediv = 255;
    hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
    HAL_RTC_Init(&hrtc);

    /* 唤醒定时器中断（低功耗 STOP 周期唤醒喂狗用） */
    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);
}
