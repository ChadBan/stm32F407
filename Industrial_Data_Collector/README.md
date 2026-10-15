# Industrial_Data_Collector — STM32F407 工业采集控制器

> 便携式电池供电多参数采集终端。分层工程化架构，覆盖 STM32 工作高频知识点 + FreeRTOS。
> 芯片 **STM32F407ZGT6** · RTOS **FreeRTOS** · HAL **STM32CubeMX + Keil-MDK**

## 1. 功能概览

- 多路采集：ADC+DMA（电池电压、2 路模拟量、4-20mA）、I2C SHT30 温湿度
- 本地显示：SSD1306 OLED，多页面（主界面 / 时间 / 参数 / 状态）
- 按键：EXTI + 软件去抖，切页 / 修改报警阈值、消音
- 通讯：RS485 Modbus-RTU 从机（DMA+IDLE 收帧，上位机读遥测、改参数）
- 存储：W25Q128 SPI Flash 掉电保存参数（带 magic+CRC+序号）
- 报警：TIM PWM 驱动蜂鸣器，温度/欠压阈值判定
- 保障：IWDG 独立看门狗 + 任务心跳监督、RTC 时间戳、STOP 低功耗
- 栈/堆监控：configCHECK_FOR_STACK_OVERFLOW + malloc failed hook

## 2. 目录结构

```
Inc/            board_config.h        唯一硬件资源映射(引脚/句柄/任务优先级)
Src/            main.c                入口 + FreeRTOS 钩子(idle->STOP/malloc/stack)
bsp/            板级驱动：gpio adc i2c spi uart rtc pwm wdg
device/         设备抽象：sht30 w25q oled modbus
app/            业务：app_types app_state + 5 个任务(collect/display/key/comm/alarm)
utils/          工具：ringbuf crc filter time
Middlewares/    FreeRTOS/FreeRTOSConfig.h
```

依赖方向单向：`app → device → bsp → (CubeMX/HAL)`，`utils` 被各层复用；
`app_state` 是唯一的全局共享状态中心（互斥锁保护，队列传递）。

## 3. 硬件资源映射（集中在 `Inc/board_config.h`）

| 外设 | 资源 | 用途 |
|------|------|------|
| ADC1 | 4 通道 + DMA 循环 | 电池/模拟量/电流，非轮询 |
| I2C1 | SHT30(0x44) + OLED(0x3C) | 温湿度 + 显示，总线互斥锁 |
| SPI1 | W25Q128(CS=PA8) | 参数/日志掉电存储 |
| USART1 | 调试 printf | fputc 重定向 |
| USART2 | RS485(DE=PA1) | Modbus-RTU，DMA+IDLE |
| TIM4-CH1 | 蜂鸣器 PWM | 报警变调鸣响 |
| RTC | 日历 + 备份域 | 采集数据时间戳 |
| IWDG | LSI | 任务卡死自动复位 |
| EXTI | PA0/PA2/PA3/PC13 | 按键 + STOP 唤醒 |

## 4. FreeRTOS 任务划分

| 任务 | 优先级 | 周期/触发 | 职责 |
|------|--------|-----------|------|
| key     | 6 | 10ms 扫描 | 去抖、切页、改参数 |
| alarm   | 5 | 100ms | 阈值判定→蜂鸣、看门狗监督 |
| comm    | 4 | 信号量 | RS485 Modbus 从机 |
| collect | 3 | 500ms | ADC+SHT30 滤波入队 |
| display | 2 | 200ms | 消费队列刷新 OLED |

看门狗策略：每任务 `bsp_wdg_task_feed()` 打心跳，`alarm_task` 调 `bsp_wdg_poll()` —
**全部任务心跳健康才真正喂 IWDG**，任一卡死→停喂→复位。

## 5. CubeMX 配置要点（本仓库只含应用层，低层 init 由 CubeMX 生成）

1. RCC：HSE + PLL → 168MHz；时钟源选择后生成 `SystemClock_Config`
2. SYS：Debug=Serial Wire；Timebase=SYS Tick（FreeRTOS 接管时改为无）
3. FreeRTOS：CMSIS-V2 或直接工程引入源码，`configUSE_IDLE_HOOK=1`
4. ADC1：Scan + Continuous + DMA2 Stream0 Circular，4 通道
5. I2C1：Fast Mode，上拉；两从机不同地址
6. SPI1：全双工，Mode0，分频到 ≤ 20MHz，CS 软件 GPIO
7. USART2：115200-8N1，开启 DMA RX(循环) + IDLE 中断，NVIC 使能
8. TIM4：PWM 输出 CH1，ARR 由 `bsp_pwm.c` 动态计算
9. RTC：Calendar，LSE/LSI；使能备份域
10. IWDG：预分频 64，重装值对应 ~3s
11. GPIO：LED(PC13 输出)、按键(输入+EXTI 上升沿)、RS485 DE(PA1 输出)
12. PWR：使能 STOP 模式，按键配 WKUP/EXTI 唤醒

> NVIC 优先级分组必须 `NVIC_PRIORITYGROUP_4`，且所有调用 `...FromISR` 的中断
> 优先级数值 ≥ `configMAX_SYSCALL_INTERRUPT_PRIORITY`（见 FreeRTOSConfig.h）。

## 6. 构建

本仓库已内置完整可编译工程（HAL + FreeRTOS + 应用层，低层初始化见
`Src/board_periph.c`，等价于 CubeMX 生成代码，已手写固化）。

1. 安装工具链：`brew install --cask gcc-arm-embedded`（或修改 Makefile 中 `CROSS` 指向本地 ARM GCC）。
2. `make` 编译出 `build/industrial_collector.{elf,hex,bin}`；`make flash` 通过 openocd + ST-Link 烧录（需自行配置 board 脚本）。
3. 调试串口 USART1(PA9/PA10, 115200) 查看 `printf` 日志。
4. 上位机 Modbus 工具按“寄存器映射”（见 `task_comm.c` 头注释）读取/写入验证。

## 7. 面试高频考点映射

- DMA 非轮询采集/收发、IDLE 线组帧 → `bsp_adc.c` / `bsp_uart.c`
- 中断与任务传数据（信号量/队列，禁阻塞）→ `bsp_uart.c` `task_comm.c` `task_collect.c`
- 总线共享的互斥锁 + 优先级继承 → `bsp_i2c.c` `bsp_spi.c`
- 任务间数据传递与快照一致性 → `app_state.c`
- 看门狗“多任务心跳监督”而非盲目喂狗 → `bsp_wdg.c`
- STOP 低功耗 + 唤醒后时钟恢复 → `main.c` `vApplicationIdleHook`
- NOR Flash 页编程/扇区擦除/CRC+序号防脏数据 → `dev_w25q.c`
- Modbus-RTU 异常码/广播/多寄存器帧校验 → `dev_modbus.c`
- 定点滤波(均值/中值/去极值/一阶低通) → `util_filter.c`

## 8. 拓展选做（冷门外设，简历加分）

CAN2.0A 上报 · SDIO 日志 · FSMC 外扩 LCD · RTC 闹钟唤醒 · OTA(IAP) 升级。
