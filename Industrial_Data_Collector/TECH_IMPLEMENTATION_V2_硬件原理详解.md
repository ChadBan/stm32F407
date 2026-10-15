# Industrial_Data_Collector 技术实现文档（零基础·硬件原理详解版）

> STM32F407ZGT6 · FreeRTOS · HAL · ARM GCC
> 面向零基础读者：先讲清楚**硬件是什么、为什么会这样**，再讲代码**怎么实现、为什么这样写**。

---

## 目录

- [第 0 章 硬件原理基础（零基础必读）](#第-0-章-硬件原理基础零基础必读)
- [第 1 章 系统启动流程](#第-1-章-系统启动流程)
- [第 2 章 采集功能](#第-2-章-采集功能)
- [第 3 章 显示功能](#第-3-章-显示功能)
- [第 4 章 按键与参数配置](#第-4-章-按键与参数配置)
- [第 5 章 Modbus 通讯](#第-5-章-modbus-通讯)
- [第 6 章 报警功能](#第-6-章-报警功能)
- [第 7 章 掉电存储](#第-7-章-掉电存储)
- [第 8 章 看门狗](#第-8-章-看门狗)
- [第 9 章 低功耗 STOP](#第-9-章-低功耗-stop)
- [第 10 章 RTC 与校时](#第-10-章-rtc-与校时)
- [第 11 章 工具库](#第-11-章-工具库)
- [第 12 章 修复记录与进度](#第-12-章-修复记录与进度)

---

# 第 0 章 硬件原理基础（零基础必读）

## 0.1 单片机是什么

把"CPU + 内存 + 硬盘 + 各种接口"压缩到一块指甲盖大的芯片上，就是单片机
（MCU）。程序写在芯片内部的 **Flash**（相当于硬盘，掉电不丢）里，上电后
CPU 从 Flash 取指令执行；运行中的变量放在 **RAM**（相当于内存，掉电丢失）。

STM32F407ZGT6 的关键数字：

| 资源 | 数值 | 通俗理解 |
|------|------|----------|
| 主频 | 168 MHz | CPU 每秒执行 1.68 亿个节拍，一个节拍做一步小操作 |
| Flash | 1 MB | 程序存储空间（本工程用了 71KB） |
| RAM | 192 KB | 变量空间（本工程任务栈+缓冲约 24KB） |
| ADC | 3 个 12bit | 把模拟电压变成数字的"测量仪" |
| UART | 6 个 | 串口通讯口 |
| SPI/I2C | 3 个/3 个 | 挂传感器/存储芯片的总线 |
| 定时器 | 12 个 | 产生节拍/PWM 的"节拍器" |

## 0.2 寄存器：外设的"控制面板"

CPU 之外，芯片上还焊着许多**外设**（串口、ADC、定时器……），每个外设都是
独立硬件，能自己干活。CPU 如何指挥它们？靠**寄存器**：

- 每个外设占一段固定的"地址区间"，区间里每个地址是一个寄存器；
- **写**某个寄存器 = 拨动外设的一个开关（比如"启动 ADC"）；
- **读**某个寄存器 = 查看外设状态（比如"串口数据到没到"）。

HAL 库做的事就是把"拨开关"封装成 `HAL_xxx()` 函数。本文后续讲的每个
`HAL_xxx` 调用，本质都是在读写寄存器。

**例**：`HAL_ADC_Start_DMA(&hadc1, buf, 16)` 背后的硬件动作是——往 ADC1
的控制寄存器写"扫描+连续+循环"配置，往 DMA 控制寄存器写源地址、目标地址、
搬运次数，然后置启动位。ADC 和 DMA 从此自己配合干活，CPU 不再参与。

## 0.3 时钟系统：一切数字电路的节拍

数字电路每一步动作都靠时钟节拍驱动。STM32 的时钟树像一棵树：

```
HSE 外部晶振 8MHz（精确，±几十 ppm）
  └─ PLL 倍频 ×21 ──> 168MHz 系统时钟 SYSCLK
        ├─ AHB 总线(内核/Flash)  168MHz
        ├─ APB2 总线(ADC/USART1) 84MHz   ← ADC 内部再分频到 ≤36MHz
        └─ APB1 总线(USART2/TIM4) 42MHz
LSI 内部 RC 振荡器 32kHz（不准，误差可达 ±50%，但独立于主时钟）
  ├─ RTC 日历时钟（128×256 分频 = 1Hz）
  └─ IWDG 看门狗计数源
```

三个关键设计决策的硬件原因：

1. **为什么 ADC 在 84MHz 总线上还要再分频**：ADC 内部逐次逼近电路最高只能
   稳定工作在约 36MHz，太快比较器来不及建立结果，所以必须预分频。
2. **为什么 RTC 和看门狗用 LSI 而不是 HSE 分频**：LSI 是独立的 RC 振荡器，
   主时钟停了（低功耗休眠）它照常走；而且 RC 电路上电即振、极省电。
3. **为什么能容忍 LSI ±50% 误差**：RTC 走时误差靠 Modbus 校时修正；看门狗
   3s 超时按最坏 2 倍误差（6s）评估仍能保护系统——只要"喂狗周期 ≪ 超时
   最坏值"就安全。

## 0.4 中断：硬件的"紧急呼叫"

CPU 按顺序执行程序时，外部事件（串口收到字节、按键按下）不该让 CPU 一直
盯着问"来了吗"，而是事件发生时由硬件**打断** CPU，跳到中断服务函数（ISR）
处理，处理完回到原处继续。NVIC 硬件按优先级仲裁多个中断。

**嵌入式第一铁律：中断里要快**。中断会打断其他所有工作（包括更高优先级
任务），在 ISR 里耗 100ms，整个系统就卡 100ms。所以本工程所有 ISR 只做
两件事：记标志、通知任务（信号量），业务全部丢给任务处理。

## 0.5 DMA：硬件搬运工

CPU 想让 16 个字节从 ADC 数据寄存器搬到内存数组，自己搬要 16 次
"读-写-自增"操作。DMA 是独立的搬运硬件：CPU 只需配置"从哪搬、搬到哪、
搬几次、搬完怎么办"，然后 CPU 该干嘛干嘛，DMA 自己把活干完。

| DMA 模式 | 行为 | 本工程用途 |
|----------|------|-----------|
| NORMAL（一次） | 搬完 N 个字节就停 | USART2 接收：一帧收完正好停，IDLE 中断里拿帧长 |
| CIRCULAR（循环） | 搬到末尾自动回起点无限搬 | ADC：16 个槽位被硬件不断用新数据覆盖 |

**为什么 ADC 用 CIRCULAR 而串口用 NORMAL**——这是理解本工程采集与收帧的
钥匙：ADC 数据"永远是最近几轮的测量值"，循环覆盖最合适；串口数据"每帧
都必须完整保留不能覆盖"，一帧搬完必须停，等任务取走后再重新挂接。

## 0.6 定时器与 PWM：计数器 + 比较器

定时器核心是一个计数器：每个时钟节拍 +1，加到 ARR（自动重装值）归零重来。
在计数器上"挂"一个比较器：计数值 < CCR（比较值）输出高，否则输出低，
就得到 PWM 方波：

```
计数:  0 ──→ CCR ──→ ARR ──→ 0 ──→ ...
输出:  1 1 1 1 1 0 0 0 0 0 1 1 1 1 1 ...
```

- 改 **ARR** = 改频率（周期长短）；
- 改 **CCR** = 改占空比（高电平比例）。

驱动无源蜂鸣器必须用方波：蜂鸣器内部是压电/电磁振膜，只有通上交变方波才
会在共振频率附近发声；直流电只能让它"咔"一下。改频就是改音调（报警变调
更有辨识度）。

## 0.7 三根总线：I2C / SPI / UART

| 特性 | I2C | SPI | UART |
|------|-----|-----|------|
| 线数 | 2（SCL 时钟 + SDA 数据） | 4（时钟 + 两数据 + 片选） | 2（TX 发 + RX 收） |
| 谁定节奏 | 主机产生时钟 | 主机产生时钟 | **双方各自用约定的波特率**，无时钟线 |
| 寻址 | 数据开头带 7bit 从机地址 | 无地址，靠 CS 片选线选芯片 | 点对点，无寻址 |
| 全/半双工 | 半双工（同一根数据线来回） | 全双工（同时收发） | 全双工 |

**I2C 为什么需要互斥锁**：SHT30 和 OLED 挂在同一条 I2C 总线上。FreeRTOS 里
采集任务和显示任务可能同时想用总线——A 任务发了半个命令，B 任务插话发自己
的地址，总线上所有芯片都懵。互斥锁保证"一个人用完，下一个人再用"。

## 0.8 Flash 存储的物理特性：为什么擦写这么麻烦

NOR Flash（W25Q128）的存储单元是浮栅晶体管：

- **写**只能把位从 1 变成 0（给浮栅充电子）；
- 要把 0 变回 1，必须**擦除**，且擦除的最小单位是 4KB 的扇区（一次擦掉
  4096 字节）；
- 写入最小单位是 256B 的**页**，且**不能跨页**（写到页边界必须停下）。
- 擦写次数有限（典型 10 万次/扇区）。

这就是为什么：改一个参数也要"读旧数据→修改→擦整个扇区→重新写入"；
也解释了为什么日志设计成 32B×256 条环形（追加写、满了才整区擦）。

## 0.9 RS485：差分信号与半双工

- **差分传输**：A/B 两根线，用两根线**电压差**表示 0/1（差为正=0，为负=1）。
  干扰会让两根线同时抬高/压低，但差值不变 → 抗干扰强、可传 1200 米。
- **半双工**：一对线既用来发也用来收，同一时刻只能一个方向。收发方向由
  DE（发送使能）引脚切换：DE=1 发送态，DE=0 接收态。
- 数据本身仍是 UART 格式：起始位 + 8 数据位 + 停止位，双方波特率必须一致。

**三个由此产生的代码设计**：
1. 发送完必须等 **TC 标志**（移位寄存器把最后一位真正挪出物理线）才把 DE
   切回接收——提前切会砍掉末字节（见 5.6 节）。
2. DE=1 时接收器被禁用，**收不到自己发出的回声**——所以从机逻辑天然不担心
   自发自收。
3. 现场多台设备并挂一对线，靠 Modbus **地址**区分谁应答（见 5.4 节）。

## 0.10 低功耗：CMOS 功耗公式

数字电路功耗 ≈ 工作电压² × 频率 × 负载。所以降功耗三招：降频、降电压、
**关时钟**。STM32 的 STOP 模式把 CPU 和大部分外设的时钟全部关掉，只留
SRAM 保持数据（μA 级电流），醒来后从头恢复时钟配置。

**看门狗与休眠的矛盾**：IWDG 由 LSI 驱动、独立于主时钟，STOP 下**照走不误**
且**无法软件关闭**（一旦启动只能复位才停）。所以休眠期间必须周期性醒来喂
一次狗——这是本工程"RTC 每 1 秒唤醒喂狗"设计的根源（见第 9 章）。

## 0.11 ADC 采样：为什么有"采样时间"参数

12bit ADC 的原理是逐次逼近：内部采样电容先充上输入电压，再和逐级生成的
参考电压比 12 次，得到 0~4095。**采样电容充电需要时间**——输入源阻抗越高
充电越慢，充不满就转换，结果偏小。本工程配置 56 周期采样时间（相对宽松），
换来了对分压电阻等较高阻抗源的适应性；代价是每通道转换时间变长。

电压换算：`电压 = 读值 × 3300mV / 4095`（参考电压 3.3V，12bit=4096 档）。

## 0.12 FreeRTOS 与裸机的区别

裸机程序是"一个大 while(1) 轮询所有事情"：一处卡住全盘皆停。FreeRTOS 把
软件切成多个**任务**（相当于多个小 while(1)），调度器让高优先级任务优先
运行、任务阻塞（等延时/等信号量）时把 CPU 让给别人：

- **任务**：独立栈的"迷你程序"，本工程 5 个（采集/显示/按键/通讯/报警）。
- **信号量**：任务间"有东西了"的通知牌。串口中断收到一帧 → 给信号量 →
  通讯任务醒来处理。
- **队列**：任务间传数据的信箱。采集任务把样本投进队列，显示任务取出。
- **互斥锁**：防"两人同时用同一资源"的锁（I2C/SPI/共享状态）。

优先级数值越大越高。本工程通讯(4) > 采集(3)：上位机写参数必须被及时响应，
不能让低优先级任务占着 CPU。

---

# 第 1 章 系统启动流程

### 为什么要这个顺序

上电后必须先"点亮系统"再"启动业务"：中断配置 → 时钟升到 168MHz → 各外设
通电配置 → 恢复上次参数 → 开显示 → 创建任务 → 调度器接管。任何一个环节
卡死，设备都起不来，所以顺序要满足依赖关系：**外设初始化相互独立可以
并行考虑，但看门狗必须最后开**。

### 代码实现（Src/main.c）

```c
int main(void)
{
    HAL_Init();              /* NVIC 优先级分组 4 + SysTick 1ms(HAL 时基) */
    SystemClock_Config();    /* HSE 8M x PLL -> 168MHz */
    bsp_init_all();          /* 见下，顺序有讲究 */
    (void)app_state_init();  /* 创建锁/队列 + 从 Flash 恢复参数 */
    (void)oled_init();       /* SSD1306 初始化序列 */
    (void)app_tasks_start(); /* 创建 5 个任务 */
    vTaskStartScheduler();   /* 调度器接管，main 不再返回 */
}
```

`bsp_init_all()` 顺序的硬件逻辑：

```c
MX_GPIO_Init();        /* 引脚/EXTI 最先 */
MX_DMA_Init();         /* DMA 时钟与 NVIC（后续 MspInit 要用） */
MX_I2C1_Init();  MX_SPI1_Init();
MX_USART1_UART_Init();  MX_USART2_UART_Init();
MX_ADC1_Init();  MX_TIM4_Init();  MX_RTC_Init();
bsp_gpio_init();  bsp_i2c_init();  bsp_spi_init();
bsp_485_init();        /* 启动 DMA+IDLE 接收 */
bsp_rtc_init();        /* 备份域标志检查，必要时设默认时间 */
bsp_pwm_init();        /* 设定频率并启动，默认静音 */
bsp_adc_init();        /* 启动 DMA 循环采样 */
bsp_wdg_init(3000);    /* IWDG 最后启动 */
```

**为什么 IWDG 必须最后启动**（第 0.9 节矛盾的另一面）：IWDG 一旦启动无法
软件关闭，只有复位能停。若先启动它，之后任何一个外设初始化卡死（例如
某芯片把 I2C 总线拉死导致 HAL 超时循环），看门狗会不断复位设备——故障
变成"无限重启"，连串口日志都打不出来，无法定位问题。让"可能卡死的环节"
都跑完再开狗，是工业固件的标准做法。

### SysTick：一个中断喂两套时钟

HAL 库用 SysTick 中断每 1ms 给 `HAL_GetTick()` 计数（大量超时判断依赖它）；
FreeRTOS 也要用 SysTick 做调度节拍。冲突的解法是分发：

```c
/* FreeRTOSConfig.h：把 FreeRTOS 的 tick 函数改名 */
#define xPortSysTickHandler SysTick_Handler_FreeRTOS

/* Src/stm32f4xx_it.c：真正的 SysTick_Handler 先喂 HAL 时基再进 FreeRTOS */
void SysTick_Handler(void)
{
    HAL_IncTick();
    SysTick_Handler_FreeRTOS();
}
```

**为什么这个 bug 致命**：若缺 `HAL_IncTick()`，`HAL_GetTick()` 永远返回 0，
所有基于它的超时判断（Flash 等待、485 发送等待、按键去抖）全部失效，
`HAL_Delay()` 死循环——设备表现就是随机卡死。这是本工程审查修复的
第 1 号问题（见 12.1 节）。

---

# 第 2 章 采集功能

### 业务逻辑（干什么）

每 500ms 一轮：取 ADC 快照（硬件 DMA 已循环采好）→ 原始值转物理量 →
滑动均值滤波 → 读 SHT30 温湿度 → 打 RTC 时间戳 → 更新共享状态 + 投递显示
队列 → 每 10 轮（5s）落一条历史日志。

### 2.1 为什么 ADC 要用 DMA 循环采集而不是轮询

4 个通道 × 12bit，若用 `HAL_ADC_GetValue` 逐通道轮询：CPU 发起转换 →
空等几十 μs → 读结果 → 再发下一通道。CPU 反复空等，且采集时刻分散在
不同时间点。DMA 方案：硬件按"通道 10→11→12→13"顺序连续扫描，每转完
一通道 DMA 自动把结果搬进内存数组，**CPU 完全不参与**，500ms 时任务只要
读内存快照。

```c
/* DMA 目标缓冲：4 通道 × 4 轮扫描，循环覆盖 */
static volatile uint16_t g_adc_dma_buf[ADC_CH_COUNT * ADC_SCAN_TIMES];

bool bsp_adc_init(void)
{
    return HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_dma_buf,
                             ADC_CH_COUNT * ADC_SCAN_TIMES) == HAL_OK;
}
```

**为什么每通道留 4 轮而不是 1 轮**：循环模式下缓冲被不断覆盖，任务读的
那一刻某槽位可能正处于"ADC 刚转换完、DMA 还没写完"的瞬间（数据竞争）。
留 4 轮再取平均：既抹平瞬时噪声，又让单轮错乱的影响降到 1/4。

```c
void bsp_adc_snapshot(uint16_t out[ADC_CH_COUNT])
{
    for (int ch = 0; ch < ADC_CH_COUNT; ch++) {
        uint32_t sum = 0;
        for (int k = 0; k < ADC_SCAN_TIMES; k++)
            sum += g_adc_dma_buf[k * ADC_CH_COUNT + ch];
        out[ch] = (uint16_t)(sum / ADC_SCAN_TIMES);
    }
}
```

**通道映射为什么是 10..13 而不是 0..3**：ADC1 通道 0..3 物理上接在
PA0..PA3，而这些引脚已被按键(PA0/PA2/PA3)和 RS485 方向脚(PA1)占用；
4-20mA/模拟量输入实际接在 PC0..PC3 = ADC1_IN10..13。代码必须与原理图
一致，否则采到的是按键电平而不是传感器信号（审查修复第 3 号）。

### 2.2 物理量换算

**电池电压**：12bit ADC 参考 3.3V，`bsp_adc_raw_to_mv = raw × 3300 / 4095`。
电池电压可能高达 4.2V（锂电）超 ADC 量程，所以原理图上先经 **1/2 分压**
再进 ADC，软件再乘回来：

```c
s.battery_mv = ma_push(&f_batt,
    (int32_t)(bsp_adc_raw_to_mv(raw[ADC_CH_BATTERY]) * 100u / BATTERY_DIVIDER_X100));
```

`BATTERY_DIVIDER_X100 = 50` 即"分压比 1/2 ×100 的定点表示"。**上板前必须
按实际电阻核对这个值**，否则电池电压显示错一倍。

**电流采样**：4-20mA 传感器回路串采样电阻 → 电流转电压 → 进 ADC。采集值
存电压 mV（`current_mv`），现场按实际阻值在上位机换算电流——比在固件里
写死电阻值更灵活。

**SHT30 温湿度**：芯片内部测完给出 16bit 原始值，数据手册换算公式：

```
T = -45 + 175 × SrT / 65535     (°C)
RH = 100 × SrH / 65535          (%)
```

代码用 ×1000 定点放大保留三位小数：

```c
int64_t t_num = (int64_t)t_raw * 175000 - 45LL * 65536 * 1000;
out->temp_x1000 = (int32_t)(t_num / 65536);
out->humi_x1000 = (uint32_t)((uint64_t)h_raw * 100000u / 65536u);
```

**为什么必须 int64/uint64**：`45 × 65536 × 1000 ≈ 29.5 亿`，超出 int32 上限
21.5 亿——int32 版本编译期就溢出成负数，温度永远算错（审查修复第 14 号）。
边界自检：raw=0 → -45.000°C；raw=65535 → ≈+130.006°C，全量程正确。

**为什么不用浮点**：Cortex-M4F 有硬件 FPU，但定点运算更快、不占浮点栈帧，
且 Modbus 传输的本来就是整数寄存器；x1000 定点即"温度 23456 = 23.456°C"。

### 2.3 为什么需要滤波

ADC 读数含两路噪声：现场电气干扰（瞬时尖峰）+ ADC 量化抖动。直接显示会
末位乱跳、报警阈值附近反复触发。滑动均值（util_filter.c）是性价比最高的
手段——O(1) 时间、内存只占一个窗口：

```c
int32_t ma_push(moving_avg_t *f, int32_t sample)
{
    if (f->cnt < f->win) {            /* 未满窗：直接追加 */
        f->window[f->idx] = sample;   f->sum += sample; f->cnt++;
    } else {                          /* 满窗：覆盖最旧样本 */
        f->sum -= f->window[f->idx];
        f->window[f->idx] = sample;   f->sum += sample;
    }
    f->idx = (f->idx + 1) % f->win;
    return f->sum / f->cnt;
}
```

窗口选择：电池/电流 8 点（慢变量，重平滑）、温湿度 5 点、模拟量 4 点
（快变量，轻平滑避免滞后）。窗口越大越平滑但响应越慢——报警延迟与噪声
抑制的权衡。

### 2.4 一轮采集的组装（task_collect.c）

```c
collect_sample_t s = {0};
uint16_t raw[ADC_CH_COUNT];
bsp_adc_snapshot(raw);
/* 电池/模拟量/电流：raw -> 物理量 -> 滤波。全部用同一份快照，
   保证时间一致性（此前电池电压调接口函数内部又做了一次快照，
   两次数值可能不同，已修复） */
...
sht30_sample_t t;
if (sht30_read(&t) && t.valid) {
    s.temp_x1000 = ma_push(&f_temp, t.temp_x1000);
    s.humi_x1000 = (uint32_t)ma_push(&f_humi, (int32_t)t.humi_x1000);
    s.valid_bits |= PARAM_VALID_BITS_TEMP | PARAM_VALID_BITS_HUMI;
}
bool time_ok = bsp_rtc_get(&s.ts);
s.valid_bits |= time_ok ? VALID_BITS_TIME : 0;

app_update_sample(&s);               /* 共享状态快照(comm/alarm 读) */
xQueueSend(app_data_queue(), &s, 0); /* 显示队列(非阻塞，满则丢) */
```

**`valid_bits` 的设计逻辑**：每一路数据带独立"有效位"。SHT30 挂掉时温湿度
位清零，其余通道照常采集上报；下游按位判断而不是整帧丢弃——单点故障不
扩散为整体失明，这正是工业"降级可用"思想。

### 2.5 历史日志为什么 5 秒一条

```c
#define LOG_EVERY_N 10   /* 10 个采样点(500ms×10=5s) */
if ((cycle % LOG_EVERY_N) == 0) {
    w25q_log_rec_t rec = { .epoch = dt_to_epoch(&s.ts), ...,
                           .alarm_bits = app_get_alarm_bits() };
    w25q_log_append(&rec);
}
```

Flash 有擦写寿命（10 万次/扇区），500ms 一条 1 天就写 17 万次，一个月写废；
5s 一条 256 条环形缓冲能存约 21 分钟历史，寿命与容量达成平衡。日志带
**当时的报警位快照**，事后可回溯"设备什么时候报过警"。

---

# 第 3 章 显示功能

### 为什么用帧缓冲 + 整屏刷新

SSD1306 内部有一块 128×64 bit 的 GDDRAM，屏幕上每个像素对应一个 bit。
写屏只能**按页**（每页 8 行像素）连续写 128 字节。若程序每显示一个字符就
发一次 I2C：字符逐字闪烁出现（撕裂感）、I2C 事务次数爆炸（一屏几十次）。

帧缓冲方案：软件里也维护一张 1KB 的"画面草稿"（`g_fb[8][128]`），渲染
只改草稿（纯内存操作、零 I2C），最后统一刷 8 次 I2C 把草稿抄上屏：

```c
bool oled_flush(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        /* 发页地址命令(0xB0+page, 0x00, 0x10) */
        ...
        buf[0] = CTRL_DATA;             /* 0x40：接下来是数据不是命令 */
        memcpy(&buf[1], g_fb[page], OLED_WIDTH);
        bsp_i2c_write(OLED_ADDR, buf, sizeof(buf), 50);
    }
}
```

### 字符怎么变成像素

5×7 字模：每个字符是一张 5 列 ×7 行的点阵图，列式存储为 7 个字节，
bit0 = 该列最上一行。例：`'A' = {0x7E,0x11,0x11,0x11,0x7E}`——第一列 7 个
像素几乎全亮(0x7E)，两侧斜杠靠 0x11。未收录字符画空心框（排查乱码来源）。

```c
uint8_t oled_puts(uint8_t x, uint8_t y, const char *s)
{
    while (*s && x + 6 <= OLED_WIDTH) {
        oled_putc(x, y, *s++);
        x = (uint8_t)(x + 6);   /* 5 列 + 1 列间隔 */
    }
    return x;
}
```

### 四个页面各显示什么、为什么

| 页面 | 内容 | 设计意图 |
|------|------|----------|
| 主界面 | 温度/湿度/电池电压 | 现场巡检第一眼关心的三件事；传感器故障显 "SENS ERR" |
| 时间页 | RTC 时间 | 判断设备时间对不对、要不要校时 |
| 参数页 | 5 个参数 + 光标 | 现场无电脑也能改参数（按键），光标指示正在编辑项 |
| 状态页 | 报警行 + 日志条数 | 报警文字错开坐标不重叠（修复第 13 号）；日志条数直读 RTC 备份寄存器，**零 SPI 开销**，200ms 刷屏无压力 |

### 刷新节奏与丢帧策略

```c
while (xQueueReceive(app_data_queue(), &s, 0) == pdTRUE)
    got = true;               /* 清空队列只留最后一帧 */
if (!got)
    app_get_last_sample(&s);  /* 无新帧则显示最近快照 */
```

显示 200ms 一帧，采集 500ms 一帧，正常情况下队列不堆积；若某次渲染慢
导致堆积，**丢弃中间帧只显示最新**——显示永远只关心"当前值"，滞后比
丢帧更糟。`vTaskDelayUntil` 保证严格周期不漂移。

---

# 第 4 章 按键与参数配置

### 硬件特性决定的软件结构

机械按键的触点按下瞬间会**弹跳**（几 ms 内通断十几次），产生一串毛刺。
若不加处理，按一下会被当作按了七八下。所以按键链路分三层：

```
按下(机械弹跳) ──> EXTI 中断:只记录"某键出现过边沿"
                 ──> 任务 10ms 扫描:边沿后 30ms 内再次出现的边沿视为抖动丢弃
                 ──> 再读电平确认确实按着,才执行动作
```

### 为什么中断里只记标志

EXTI 中断在弹跳期间可能触发十几次，若每次中断都执行按键动作，去抖无从
谈起。正确做法：ISR 只把"该键出现过边沿"写进一个位图，任务侧原子消费：

```c
static volatile uint8_t g_key_edge;  /* bit i = 键 i 出现过边沿 */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    for (uint8_t i = 0; i < KEY_ID_MAX; i++)
        if (g_key_pins[i].pin == GPIO_Pin) { g_key_edge |= 1u << i; break; }
}

bool bsp_key_take_edge(key_id_t id)
{
    taskENTER_CRITICAL();            /* 读-改-写必须与 ISR 互斥 */
    bool had = (g_key_edge & bit) != 0;
    g_key_edge &= ~bit;
    taskEXIT_CRITICAL();
    return had;
}
```

`taskENTER_CRITICAL` 是关中断的临界区：若无此保护，任务读到一半 ISR 恰好
置位，边沿可能丢失（读-改-写被拆开）。

### 去抖与页面逻辑（task_key.c）

```c
for (int k = 0; k < KEY_ID_MAX; k++) {
    if (!bsp_key_take_edge(k)) continue;
    if (now - last_ms[k] < KEY_DEBOUNCE_MS) continue;  /* 30ms 内视为抖动 */
    last_ms[k] = now;
    if (!bsp_key_pressed(k)) continue;                 /* 边沿后电平再确认 */
    app_mark_activity();           /* 任何按键刷新活动时间，抑制休眠 */

    disp_page_t page = app_get_page();
    if (page != PAGE_PARAM) {
        /* OK/UP/DOWN 循环切页 */
        continue;
    }
    /* ---- 参数页 ---- */
    if (up || down) {
        /* 按字段步进：温度 ±0.5°C(x10 单位 ±5)、电池 ±50mV、地址 ±1、波特率码 ±1 */
        if (changed) app_set_params(&p);   /* 改一次落一次盘 */
    } else if (ok) {
        app_set_param_field((field + 1) % PARAM_FIELD_COUNT);  /* 光标前进 */
        app_set_params(&p);
    }
}
```

### 参数编辑约束与落盘时机

- 边界钳制：电池阈值 2500..5000mV、从机地址 1..247、波特率码 0..4，越界
  不改——Modbus 协议规定地址 0 是广播、247 是保留地址，从机不能用。
- **改一次落一次盘**：立即持久化，断电不丢（Flash 擦写寿命足够按键级
  频率）；不采用"长按保存"是因为现场无确认机制，丢了参数比多擦几次更糟。
- 按键与 Modbus 共用同一套 `app_set_params` 校验+落盘路径，两个入口行为
  完全一致。

---

# 第 5 章 Modbus 通讯

### 物理层到应用层的全链路

```
RS485 总线(差分) ──> SN75176 类收发器(DE 控制方向) ──> USART2 引脚
  ──> 硬件移位寄存器收字节 ──> DMA 搬到内存 ──> IDLE 中断通知任务
  ──> ringbuf 缓冲 ──> dev_modbus 协议栈 ──> 寄存器回调 ──> 业务动作
```

### 5.1 为什么用 DMA + IDLE 中断收帧

Modbus-RTU 帧**没有帧长字段**，靠总线静默 3.5 字符时间分隔两帧。硬件层面
恰好有对应的机制：UART 的 **IDLE 中断**——线上一个字节时间没有数据就触发。
一帧发完必然静默，所以"IDLE 中断发生"≈"一帧收完"：

```
总线: [一帧字节][静默] [一帧字节][静默] ...
硬件: DMA 逐个搬字节     ↑ IDLE 中断
软件: 把 DMA 已收字节落 ringbuf -> 唤醒通讯任务 -> 重新挂接 DMA 等下一帧
```

用逐字节中断替代方案对比：每收到一个字节就打断 CPU 一次，115200 波特率下
每 87μs 一次中断，CPU 忙不过来。DMA 把收字节也外包给硬件，一帧才打断
一次。

### 5.2 为什么 DMA 用 NORMAL 而非 CIRCULAR

IDLE 中断发生时 HAL 已停止 DMA，回调里能拿到**本次帧长**，重新挂接即可。
若用 CIRCULAR：缓冲首尾相接，帧跨过缓冲区末尾时首部数据已被覆盖，帧长
要靠"缓冲区大小 - 剩余计数"反推且可能在回绕边界出错。串口场景"帧必须
完整保留"，NORMAL 恰好与 IDLE 停流的时机完美配合。

**错误恢复**（溢出/噪声/帧错误触发 `HAL_UART_ErrorCallback`）：停 DMA →
清 ringbuf → 重挂接收。RS485 现场常有干扰毛刺制造假起始位，若无此恢复，
一次错误后 DMA 永久卡死，通讯任务永远等不到信号量——设备"假死"。

### 5.3 通讯任务主循环与多帧解析

```c
for (;;) {
    if (xSemaphoreTake(bsp_485_frame_sem(), pdMS_TO_TICKS(1000)) == pdTRUE) {
        uint16_t n = rb_read(bsp_485_rxbuf(), req, sizeof(req));
        uint16_t off = 0;
        while (n > 0 && off < n) {        /* 一次唤醒可能已缓冲多帧 */
            uint16_t fl = modbus_frame_len(&req[off], n - off);
            if (fl == 0) break;           /* 不足一帧/CRC 坏，丢弃 */
            uint16_t rn = modbus_process_frame(&cfg, &req[off], fl,
                                               resp, sizeof(resp));
            if (rn > 0) bsp_485_send(resp, rn, 100);
            off += fl;
        }
    }
    bsp_wdg_task_feed(WDG_TASK_COMM);     /* 1s 超时也喂心跳 */
}
```

**为什么要循环解析**：上位机连发多帧（如轮询多个寄存器）时，一次 IDLE
唤醒的 ringbuf 里可能躺着好几帧。此前只处理首帧、其余丢弃（修复第 8 号）。

### 5.4 帧长推断（dev_modbus.c）

```c
uint16_t modbus_frame_len(const uint8_t *buf, uint16_t len)
{
    switch (buf[1]) {
    case FC_READ_HOLDING: case FC_READ_INPUT: case FC_WRITE_SINGLE:
        need = 8;               break;   /* addr+fc+2+2+2CRC */
    case FC_WRITE_MULTI:
        if (len < 7) return 0;
        need = 9 + buf[6];      break;   /* 固定头 9B + byteCnt 个数据 */
    default:
        need = 8;               break;
    }
    if (need > len || !crc16_modbus_check(buf, need)) return 0;
    return need;
}
```

### 5.5 协议栈处理流程（dev_modbus.c）

`modbus_process_frame(cfg, req, len, resp, cap)` 的处理顺序与理由：

1. **CRC 错 → 静默丢弃**。Modbus 规范明确要求：CRC 错的帧必须忽略，
   连异常响应都不能回（否则噪声触发应答风暴）。
2. **地址不匹配且非广播 → 静默丢弃**。一条总线上挂着多台从机，主机轮询
   别家时本机不许出声。
3. **广播（addr=0）**：写操作**执行但不响应**——多台从机同时应答会在总线
   上打架（RS485 半双工物理特性决定）。
4. 功能码分支：

**0x03/0x04 读寄存器**：
```
校验 req_len==8、数量 1..125
-> 逐寄存器调读回调；任一地址非法 -> 异常码 0x02
-> 组装 [addr|fc|byteCnt|data...|CRC]
```

**0x06 写单个**：成功回显原帧（Modbus 规定从机原样回显表示确认）；失败
回异常 0x02。**0x10 写多个**：校验 byteCnt == qty×2 与帧长一致性，逐寄存器
写；任一失败回异常（已写的不回滚——Modbus 从机语义不要求事务性）。

**未知功能码 → 异常 0x01**：`[addr | fc|0x80 | code | CRC]`。功能码高位置 1
是"异常"标志，主机据此识别。

### 5.6 响应发送：为什么要等 TC

```c
bool bsp_485_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    RS485_SET_TX();                       /* DE=1 进发送态 */
    HAL_StatusTypeDef st = HAL_UART_Transmit(&huart_485, data, len, timeout_ms);
    uint32_t t0 = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(&huart_485, UART_FLAG_TC) == RESET)
        if ((HAL_GetTick() - t0) > timeout_ms) break;
    RS485_SET_RX();                       /* DE=0 回接收态 */
    return st == HAL_OK;
}
```

硬件细节：UART 发送器有**两级缓冲**——数据寄存器（TDR，立即写）和
**移位寄存器**（把字节一位一位挪到物理线上，慢）。`HAL_UART_Transmit`
返回时最后一字节刚进 TDR，移位寄存器还在挪最后几位；此刻切回接收态，
线上最后几位还没发出去就被砍掉 → 主机 CRC 校验失败，从机表现为"有时
不应答"（修复第 4 号）。**TC 标志 = 移位寄存器已空**，等它才切方向。

### 5.7 寄存器映射与回调（task_comm.c）

协议栈与业务通过三个回调解耦：

**读输入回调**（0x04）：

| 地址 | 内容 | 实现要点 |
|------|------|----------|
| 0-5 | 温度×10/湿度×10/电池mV/模拟量1/2/电流mV | `app_get_last_sample` 取共享快照；x1000 转 x10 直接除 100 |
| 6-9 | 年/(月<<8\|日)/(时<<8\|分)/秒 | RTC 拆字 |
| 10 | 日志总条数 | 直读 RTC 备份寄存器，零 SPI 开销 |
| 11-19 | 游标历史记录 | 预取机制，见下 |

**游标预取**：写保持寄存器 6（日志游标）时立即 `prefetch_cursor(val)` 读
Flash 把该条记录缓存到 `g_rec`；之后主机读 11-19 直接出缓存，**读多次不再
碰 SPI**（上位机一次读 9 个寄存器只触发一次 Flash 读，否则 Flash 随机读
要毫秒级，协议超时会丢响应）。

**写回调要点**：
- **校时两段式**：0x10-0x13 写暂存区 `g_time_stage`，0x14 写 1 才
  `bsp_rtc_set` 一次性应用——若每写一个寄存器就改 RTC，主机写一半断电会
  得到"年月日已改、时分秒还是旧的"的错乱时间。
- **波特率联动**：reg 4 写入后立即 `bsp_485_set_baud_code`——停接收 → 改
  BRR → `HAL_UART_Init` → 重挂 DMA。约定：上位机写完波特率码后切到新
  波特率等响应（旧波特率上不再有应答是正常的）。
- 参数写走 `app_set_params`（校验 + 落盘），与按键入口一致。

---

# 第 6 章 报警功能

### 业务逻辑

100ms 周期：读最新采样与参数 → 阈值判定 → 更新报警位 → 非静音时驱动蜂鸣
间歇鸣响 → 兼任看门狗监督点。

### 6.1 阈值判定为什么有这么多"门卫条件"

```c
uint8_t bits = 0;
if (have && (s.valid_bits & PARAM_VALID_BITS_TEMP)) {
    sht_fail_cnt = 0;
    int16_t temp_x10 = (int16_t)(s.temp_x1000 / 100);
    if (temp_x10 > p.temp_alarm_hi) bits |= ALM_BIT_TEMP_HI;
    if (temp_x10 < p.temp_alarm_lo) bits |= ALM_BIT_TEMP_LO;
} else if (sht_fail_cnt < UINT32_MAX) {
    sht_fail_cnt++;
}
if (sht_fail_cnt >= 30) bits |= ALM_BIT_SENSOR;  /* 连续 3s 无效=故障 */
if (have && (s.valid_bits & PARAM_VALID_BITS_BATT))
    if (s.battery_mv > 0 && s.battery_mv < p.batt_alarm_mv)
        bits |= ALM_BIT_BATT_LO;
app_set_alarm_bits(bits);              /* 全量覆盖写 */
```

- **温度无效时不判高低温**：传感器断了还拿上一次的旧值判，可能报出假
  高温/假低温，误导现场。无效就明说"传感器故障"，不假装有数。
- **连续 30 个周期（3s）才置故障位**：容忍偶发 I2C 抖动（1 次失败不报警），
  持续 3s 失败才认定真故障——防误报与快响应之间的折中。
- **`battery_mv > 0` 防初始零值误报**：上电第一拍采样还没就绪时快照为 0，
  若不设此门卫，会瞬间报"电池欠压"再消失，蜂鸣器乱叫。

### 6.2 蜂鸣相位：为什么间歇鸣响

```c
bool alarming = (bits != 0) && !p.mute;
if (alarming) {
    uint32_t now = HAL_GetTick();
    if (now - last_toggle >= (buzz_phase ? BUZZ_ON_MS : BUZZ_OFF_MS)) {
        buzz_phase = !buzz_phase;      /* 鸣 120ms / 停 400ms */
        last_toggle = now;
        bsp_pwm_beep(buzz_phase);
    }
} else {
    bsp_pwm_beep(false);               /* mute 或解除立即静音 */
    buzz_phase = false;
}
```

间歇式报警（滴-滴-滴）比持续长鸣更醒目且省电；`mute`（消音）经 Modbus
保持寄存器 5 控制——远程有人值守时关掉现场蜂鸣。蜂鸣驱动改频后占空比
必须按**实际 ARR** 算（`Init.Period` 不随改频更新，修复第 9 号），否则
占空比畸变导致音量忽大忽小。

### 6.3 报警为何兼任看门狗监督

监督点需要一个"自己永远在周期跑"的任务；alarm 任务 100ms 周期是全场
最严格的心跳，把它作为统一喂狗点，监督逻辑与报警业务合并不增加任务数
（详见第 8 章）。

---

# 第 7 章 掉电存储

### 硬件铁律与设计推导

W25Q128 是 NOR Flash（16MB）：
1. 只能把 1 写 0，0 变 1 必须**擦除**，擦除最小单位 **4KB 扇区**；
2. 写入最小单位 **256B 页**，不能跨页；
3. 擦写寿命约 10 万次/扇区。

由此推出两块区域的设计：

| 区域 | 位置 | 数据 | 写入策略 | 寿命分析 |
|------|------|------|----------|----------|
| 参数区 | sector 0 | 设备参数(≤32B) | 改参数 → 擦扇区 → 整扇区重写 | 改 10 万次参数才写坏 |
| 日志区 | sector 16 起 8KB | 256 条×32B 历史 | 追加写，写满整区擦 | 5s 一条 ≈ 21 分钟一圈 |

### 7.1 为什么命令和地址必须在一段 CS 内

SPI 的 CS（片选）下降沿=命令开始，**上升沿=命令立即终止**。Flash 的页编程
时序是"命令(0x02) + 3 字节地址 + 数据"，任何中途拉高 CS 都会被芯片视为
命令结束——只收到地址没收到数据，写入失败。所以必须一次连续传输：

```c
/* bsp_spi.c：CS 拉低 -> 命令头 -> 数据段 -> CS 拉高，全程不断 */
bool bsp_spi_xfer_flash(const uint8_t *hdr, uint16_t hdr_len,
                        const uint8_t *tx, uint8_t *rx,
                        uint16_t len, uint32_t timeout_ms)
{
    bsp_spi_lock();  bsp_spi_cs_low();
    ok = HAL_SPI_Transmit(hdr, hdr_len)     /* 命令+地址 */
      && data_phase(tx, rx, len);           /* 数据段 */
    bsp_spi_cs_high();  bsp_spi_unlock();
    return ok;
}
```

此前代码命令与数据分两次调用（中间 CS 拉高），Flash 读回的永远是 0xFF
（修复第 2 号）。读数据方向用 64B dummy 分块换数据，避免大缓冲。

### 7.2 参数区：magic + 序号 + CRC 三重保险

```
布局: [magic 0xCD78(2B) | seq(2B) | len(2B) | crc16(2B)] + payload
```

**保存**：读旧头部取 seq+1 → 打包 → 擦扇区 → 页编程。
**加载**（app_state.c 启动时）三重校验：magic → len 范围 → CRC，任一失败
回退默认参数并立即落盘。

**为什么需要这些保险**：设备可能在"擦除完成、写入一半"时断电（工业现场
掉电不可控）。此时 Flash 里是半截数据，若软件不校验直接使用，可能得到
"地址 0x00 或 0xFF"之类非法参数。校验失败 → 回退默认值 → 设备以安全
参数启动，这是掉电鲁棒性的标准做法。

**合法性钳制**：即使 CRC 恰好通过（极低概率）也不信任全部值：

```c
if (g_params.modbus_addr == 0 || g_params.modbus_addr > 247)
    g_params.modbus_addr = PARAM_DEFAULT.modbus_addr;
if (g_params.baud_code > 4) g_params.baud_code = 0;
```

### 7.3 日志区：环形追加 + 备份域指针

**写指针为什么存 RTC 备份寄存器**：备份域由 VBAT 电池独立供电，主电掉电
指针不丢；若存普通 RAM 掉电归零，重启后要么从头覆盖要么查不清写到哪了。
（无 VBAT 时指针归零，从头覆盖——行为可接受。）

```c
uint32_t total = log_total_read();
uint32_t slot = total % W25Q_LOG_CAPACITY;   /* 256 槽 */
if (slot == 0 && total != 0) {               /* 写满一圈回绕 */
    for (s = 0; s < LOG_SECTORS; s++)
        w25q_sector_erase(LOG_BASE_ADDR + s * 4096);
    total = 0;                               /* 计数清零(修复第 6 号) */
}
/* 组装 32B 槽(0xFF 预填充) -> 单页编程(32B 不跨页) */
w25q_page_write(log_slot_addr(slot), buf, 32);
log_total_write(total + 1);
```

**32B 为什么重要**：256B 页 ÷ 32B = 8 条/页，槽边界永远在页边界内，永不
跨页——"不跨页"约束被数据结构天然满足，无需软件拆分。

**读取 idx=0 为最新**：`slot = (total-1-idx) % 256`，逐槽校验 magic+CRC，
坏槽返回 false 由上位机跳过（擦写瞬间断电可能产生坏槽，单条损坏不影响
其余历史）。

### 7.4 为什么日志条数显示不读 Flash

`w25q_log_count()` 直接读备份寄存器。若改成读 Flash 统计，状态页 200ms
刷新一次就要 200ms 读一次 SPI——既不必要又伤 Flash。硬件上一枚寄存器就
解决的事，绝不加软件负担。

---

# 第 8 章 看门狗

### 硬件原理

IWDG 是一个独立的硬件倒计时器：从初值开始每个 LSI 节拍减 1，减到 0 直接
**硬件复位**整个芯片。程序必须在归零前调 `HAL_IWDG_Refresh` 重装初值
（俗称"喂狗"）。一旦启动，软件无法关闭——只能靠复位。它的意义：程序
跑飞（栈溢出、死锁、被总线拉死）时，没有任何"软件"能自救，只有硬件
能兜底复位。

### 为什么不能"每个任务各自喂狗"

传统做法每个任务自己喂狗：任务 A 卡死，但任务 B、C 还在喂，狗永远饿不
死，系统带病运行没人知道。**单一监督点 + 多任务心跳**：每个任务只记录
自己的心跳时间戳，由 alarm 任务统一检查"所有人都在跳"才喂狗：

```c
static volatile uint32_t g_heartbeat[WDG_TASK_NUM];

void bsp_wdg_task_feed(wdg_task_id_t id) { g_heartbeat[id] = HAL_GetTick(); }

bool bsp_wdg_poll(uint32_t task_timeout_ms)   /* alarm 任务每 100ms 调 */
{
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < WDG_TASK_NUM; i++)
        if ((uint32_t)(now - g_heartbeat[i]) > task_timeout_ms)
            return false;      /* 有任务卡死：故意不喂 -> 复位 */
    HAL_IWDG_Refresh(&g_hiwdg);
    return true;
}
```

### 时间参数的两层数学关系

- IWDG：LSI 32kHz / 预分频 64 = 500Hz 计数，重装 1500 ≈ **3s 超时**；
- 任务心跳超时窗口 = **2s** < 3s：任何任务卡死，2s 内被识别、停止喂狗，
  3s 时硬件复位——检测先于动作，逻辑闭环。
- **无符号减法防回绕**：`HAL_GetTick()` 是 uint32，49.7 天回绕一次。
  `(uint32_t)(now - last)` 在回绕时依然正确（无符号数的减法模运算特性），
  设备连续运行数月不会因回绕误判卡死。

---

# 第 9 章 低功耗 STOP

### 核心矛盾的由来

- STOP 模式关掉主时钟和 CPU，电流降到 μA 级——电池供电设备必须用；
- 但 IWDG 由 LSI 驱动、独立走表、**软件关不掉**（第 8 章原理）。

结论：休眠期间必须周期性醒来喂狗，否则 3s 后看门狗把设备复位——设备
会陷入"睡着→被狗咬醒→再睡→再咬醒"的无限循环。

### 进入条件（三把锁全开才睡）

```c
static bool sleep_allowed(void)
{
    device_params_t p;
    app_get_params(&p);
    return p.sleep_en                       /* 参数区开关，默认关 */
        && app_get_alarm_bits() == 0        /* 报警期间不休眠 */
        && app_ms_since_activity() > SLEEP_IDLE_MS;  /* 30s 无活动 */
}
```

- `sleep_en` 默认关：现场调试期绝不允许设备自己睡着（否则"设备没反应"
  分不清是坏是睡）。
- 报警期间不休眠：报警状态下蜂鸣/显示必须持续工作。
- 活动时间戳由按键任务、通讯任务收到帧时刷新——上位机轮询期间不会睡。

### 休眠与唤醒全过程

```c
void vApplicationIdleHook(void)   /* 所有任务都阻塞时被调度器反复调用 */
{
    if (!sleep_allowed()) { __WFI(); return; }

    HAL_SuspendTick();                          /* 关 SysTick */
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 0, RTC_WAKEUPCLOCK_CK_SPRE_17BITS);
    /* 1s 周期唤醒：RTC 1Hz 分频源 + 计数器 0 = 下一秒触发 */
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* ---- 唤醒后 ---- */
    SystemClock_Config();         /* STOP 后时钟回退到 HSI，重建 168MHz */
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    HAL_ResumeTick();
    (void)bsp_adc_init();         /* STOP 期间 ADC/DMA 被停，重启 */
    (void)bsp_485_init();         /* 485 DMA 接收同样要重启 */
    bsp_wdg_force_refresh();      /* 关键：见下 */
}
```

**为什么唤醒周期 1s 而不是更省电的更长间隔**：LSI 误差可达 ±50%，最坏
情况下 1s 唤醒可能变成 1.5s，仍远小于 3s 超时——余量设计。若贪心设 2.5s，
LSI 偏慢时实际 3.75s，狗已经咬人了。

**为什么 `bsp_wdg_force_refresh` 强制刷新所有心跳**：休眠期间 5 个任务全部
冻结（调度器不跑），它们的心跳自然全"停跳"。唤醒后若不强制刷新，监督
逻辑会误判"所有任务卡死"而停喂 → 复位。休眠是计划内的冻结，不是故障，
必须与真卡死区分。

**唤醒来源**：RTC 唤醒定时器（周期喂狗）或按键 EXTI（PA0 兼作 WKUP，
用户按键唤醒）。任一唤醒都走同一段恢复流程。

---

# 第 10 章 RTC 与校时

### 10.1 时间源与初始化

RTC 是独立的日历硬件：LSI 32kHz 经 128×256 分频得 1Hz，内部维护
年月日时分秒寄存器。备份域 `BKP_DR0` 存 0xA55A 标志：

```c
if (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) != RTC_MAGIC) {
    /* 首次上电(或备份域掉电)：设默认时间，避免 1970 年 */
    bsp_rtc_set_default();      /* 2026-01-01 00:00:00 */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, RTC_MAGIC);
}
```

**为什么默认 2026 而不是 1970**：`dt_to_epoch` 以 2000-01-01 为纪元，1970
年日期会算出负数 epoch，日志时间错乱。默认值保证"没校时也有合理时间"。

**读时间必须 GetTime 后紧跟 GetDate**：RTC 硬件把时间/日期锁存在影子
寄存器，GetTime 的瞬间硬件锁存当时值，紧接着读 GetDate 才能保证日期与
时间属于同一瞬间（否则 23:59:59.99 读时间、下一秒读日期，日期跳了一天）。

### 10.2 为什么校时是"两段式"

Modbus 主机校时步骤：
```
0x10 写 reg 0x10..0x13 (年/月日/时分/秒) -> 从机暂存 g_time_stage
0x06 写 reg 0x14 = 1                      -> bsp_rtc_set 一次性应用
```

若"写一个寄存器就立即改一次 RTC"：主机写到一半断电或异常，RTC 停在
"年已改、秒未改"的中间态。两段式 = 事务式提交，最后一条命令才是
commit。`bsp_rtc_set` 前先 `dt_is_valid` 校验（年月日/时分秒范围、闰年
二月天数），非法时间拒绝写入并回异常——防止把 RTC 写成 2 月 30 日。

### 10.3 时间戳如何进入日志

采集任务 `bsp_rtc_get(&s.ts)` → `dt_to_epoch` 转成 2000 纪元秒数（uint32，
够用到 2099 年）→ 存进日志记录。上位机读历史时 epoch 再转回日历时间
显示。

---

# 第 11 章 工具库

### 11.1 util_ringbuf：为什么无锁且安全

使用场景：USART2 中断**写**、通讯任务**读**——两个执行流并发访问。
若用普通数组+锁：中断里不能阻塞等锁（会死锁/丢数据）。

解法是单生产者/单消费者环形缓冲：
- 容量 2 的幂 → `& mask` 取模（免除法，ISR 友好）；
- **预留 1 字节**：可写空间 = size-1，避免 head==tail 时"空/满"二义性；
- 写侧只动 head、读侧只动 tail，各管各的游标，天然无竞争：

```c
uint16_t rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len)
{
    uint16_t head = rb->head;
    while (written < len) {
        uint16_t next = (head + 1) & rb->mask;
        if (next == rb->tail) break;       /* 满：丢新数据 */
        rb->buf[head] = data[written++];
        head = next;
    }
    rb->head = head;   /* 数据全写完才更新游标，读者永远看不到半帧 */
}
```

写满静默丢新帧（串口场景宁丢新帧不坏旧帧）；游标最后更新保证读者视角
一致性。

### 11.2 util_filter：四种滤波各管什么场景

| 函数 | 应对噪声 | 代价 |
|------|----------|------|
| 滑动均值 `ma_push` | 白噪声/量化抖动 | 响应滞后（窗口越大越钝） |
| 中值 `median_filter` | 脉冲毛刺（电机打火） | O(n log n)，排序取中间值 |
| 去极值均值 `trimmed_mean` | 偶发大尖峰 | 丢掉最值信息 |
| 一阶低通 `lowpass1` | 高频干扰，平滑性最好 | 相位滞后最大，系数 1024 定点 |

工程当前用滑动均值（O(1) 且够用），其余为备用件——现场遇到特定干扰再换。

### 11.3 util_crc：Modbus 专用校验

CRC16-Modbus：多项式 0xA001（0x8005 反射形式）、初值 0xFFFF，逐位查表
等价实现；**传输低字节在前**——Modbus 约定与 TCP/IP 相反，写死记牢。
接收方算一遍对不上即弃帧（第 5.5 节），保证电气噪声制造的假帧进不了
业务层。

### 11.4 util_time：为什么不直接用标准库

- 手写 `dt_to_epoch`（2000 纪元，逐月累加天数）：无 `time.h` 依赖、
  代码透明可验证、ROM 占用可控；
- `dt_format` 手写整数转字符串：`snprintf` 在嵌入式裸机/RTOS 环境要拉
  进完整 printf 变参机制（几 KB 代码 + 不确定的栈消耗），格式固定时
  手写更稳；
- 范围 2000-2099 与 RTC 硬件字段宽度严格对应，越界输入直接拒绝。

---

# 第 12 章 修复记录与进度

### 12.1 审查修复清单（16 项）

| # | 问题 | 根因 | 修复 |
|---|------|------|------|
| 1 | HAL_GetTick 冻结/HAL_Delay 死循环（致命） | FreeRTOS 接管 SysTick 无人喂 HAL 时基 | SysTick_Handler 分发 |
| 2 | W25Q 读写无效（致命） | 命令与数据分两次传输，CS 中途拉高终止命令 | bsp_spi_xfer_flash 单段 CS |
| 3 | ADC 通道错误（致命） | 采 IN0-3(PA0-3 被按键/DE 占用)，模拟脚在 PC0-3 | ADC_CHANNEL_10..13 |
| 4 | 485 末字节截断 | TC 自旋等待无延时提前超时 | HAL_GetTick 计时 |
| 5 | comm 栈溢出风险 | req/resp 256B×2 + 落盘缓冲 256B > 1KB | 栈 512 words |
| 6 | 日志回绕读不出 | 擦除后计数未复位 | 擦除后计数清零 |
| 7 | 波特率码不生效 | 只存参数未重配 USART2 | bsp_485_set_baud_code |
| 8 | 粘包丢帧 | 一次唤醒只处理首帧 | modbus_frame_len 循环解析 |
| 9 | 蜂鸣占空比失真 | 用 Init.Period 而 ARR 已变 | 取实际 ARR |
| 10 | 传感器故障无报警 | ALM_BIT_SENSOR 未使用 | 连续 3s 置位 |
| 11 | 电流字段语义不符 | current_ua_x10 实存 mV | 改名 current_mv |
| 12 | 电池电压两次快照 | 接口函数内部二次快照 | 同一份快照 |
| 13 | 状态页行覆盖 | TEMP HI/LO 同坐标 | 错开 + 补 SENS |
| 14 | SHT30 换算溢出 | int32/uint32 中间值超范围 | int64/uint64 |
| 15 | 启动文件不兼容 | IAR 语法 + GCC 工具链 | ST 官方 GCC 版 |
| 16 | 互斥锁链接失败 | configUSE_MUTEXES 未开 | 显式开启 |

### 12.2 构建、烧录与验证

```bash
cd Industrial_Data_Collector && make     # 零警告，Flash 71KB / RAM 24KB
make flash                               # openocd + ST-Link
```

验证步骤：USART1 看日志 → OLED 四页面按键切换 → Modbus 工具按 5.7 节映射
读写 → 加热/断电传感器验证报警 → 掉电重启验证参数/日志 → 开 sleep_en 静置
30s 验证休眠唤醒。

### 12.3 进度

- ✅ 全部代码完成，编译零警告；16 项审查问题全部修复。
- ⏳ 待硬件联调：按实际原理图核对 `board_config.h`（分压比/I2C 地址/蜂鸣
  频率/DE-RE 并联方式）后烧录整机验证。
- ⬜ 拓展选做：CAN2.0A 上报、SDIO 日志、FSMC LCD、RTC 闹钟唤醒、OTA。
- ⬜ 可选加固：参数区双备份扇区、按键消音入口、日志写保护。
