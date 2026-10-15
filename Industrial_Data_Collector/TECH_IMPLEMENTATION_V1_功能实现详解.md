# Industrial_Data_Collector 技术实现文档（功能实现详解版）

> STM32F407ZGT6 · FreeRTOS V10.4.6 · HAL · ARM GCC 工具链
> 便携式电池供电多参数工业采集控制器
> 本文按"功能 → 业务逻辑 → 代码实现"逐条展开，所有代码均为仓库当前实现。
> 面向零基础的硬件原理解读见姊妹篇《TECH_IMPLEMENTATION_V2_硬件原理详解.md》。

---

## 目录

1. [系统启动流程](#1-系统启动流程)
2. [采集功能](#2-采集功能-task_collect)
3. [显示功能](#3-显示功能-task_display--dev_oled)
4. [按键与参数配置](#4-按键与参数配置-task_key)
5. [Modbus 通讯](#5-modbus-通讯-task_comm--dev_modbus--bsp_uart)
6. [报警功能](#6-报警功能-task_alarm)
7. [掉电存储](#7-掉电存储-dev_w25q)
8. [看门狗监督](#8-看门狗监督-bsp_wdg)
9. [低功耗 STOP](#9-低功耗-stop-mainc-空闲钩子)
10. [RTC 与校时](#10-rtc-与校时)
11. [工具库实现](#11-工具库实现)
12. [修复记录与进度](#12-修复记录与进度)

---

## 1. 系统启动流程

### 业务逻辑

上电后依次完成：内核/HAL 初始化 → 168MHz 时钟 → 各外设初始化 → 状态中心
（含 Flash 参数恢复）→ OLED → 创建 5 个业务任务 → 启动调度器。

### 代码实现（Src/main.c）

```c
int main(void)
{
    HAL_Init();              /* NVIC 分组 4 + SysTick 1ms(HAL 时基) */
    SystemClock_Config();    /* HSE 8M x PLL -> 168MHz */
    bsp_init_all();          /* 见下 */
    (void)app_state_init();  /* 创建锁/队列 + 从 Flash 恢复参数 */
    (void)oled_init();       /* SSD1306 初始化序列 */
    (void)app_tasks_start(); /* 创建 5 个任务 */
    vTaskStartScheduler();   /* 不再返回 */
}
```

`bsp_init_all()` 的初始化顺序是有讲究的：

```c
MX_GPIO_Init();       /* 引脚/EXTI 最先，供后续 MspInit 复用参考 */
MX_DMA_Init();        /* DMA 时钟与 NVIC */
MX_I2C1_Init();  MX_SPI1_Init();
MX_USART1_UART_Init();  MX_USART2_UART_Init();
MX_ADC1_Init();     MX_TIM4_Init();  MX_RTC_Init();
bsp_gpio_init();  bsp_i2c_init();  bsp_spi_init();
bsp_485_init();      /* 启动 DMA+IDLE 接收 */
bsp_rtc_init();      /* 备份域标志检查，必要时设默认时间 */
bsp_pwm_init();      /* 设定频率并启动，默认静音 */
bsp_adc_init();      /* 启动 DMA 循环采样 */
bsp_wdg_init(3000);  /* IWDG 最后启动：前面 init 卡死不触发复位误伤 */
```

关键细节：**IWDG 必须最后初始化**。IWDG 一旦启动不可软件关闭，若先启动而
某个外设 init 卡住（如总线拉死），设备会一直复位而无法定位问题。

### SysTick：HAL 时基与 FreeRTOS 并存

FreeRTOS 接管 SysTick 后不再调用 `HAL_IncTick()`，而工程大量使用
`HAL_GetTick()/HAL_Delay()`（看门狗心跳、去抖、Flash 等待、蜂鸣相位），
因此做了分发处理：

```c
/* FreeRTOSConfig.h：把 port.c 的 tick 函数改名 */
#define xPortSysTickHandler SysTick_Handler_FreeRTOS

/* Src/stm32f4xx_it.c：真正的 SysTick_Handler 先喂 HAL 时基再进 FreeRTOS */
void SysTick_Handler(void)
{
    HAL_IncTick();
    SysTick_Handler_FreeRTOS();
}
```

---

## 2. 采集功能（task_collect）

### 业务逻辑

每 500ms 完成一轮：读 ADC 快照（DMA 循环采好的数据）→ 原始值转物理量 →
滑动均值滤波 → 读 SHT30 温湿度 → 打 RTC 时间戳 → 更新共享状态 + 投递队列
→ 每 10 轮（5s）落一条历史日志。

### 2.1 ADC DMA 循环采集（bsp_adc.c）

**为什么不用轮询**：4 通道 × 12bit 若用 `HAL_ADC_GetValue` 逐通道查询，
CPU 空转等待转换完成；DMA 循环模式由硬件自动把每次转换结果搬运到内存，
任务只需读内存快照。

```c
/* DMA 目标缓冲：4 通道 × 4 轮扫描，循环覆盖 */
static volatile uint16_t g_adc_dma_buf[ADC_CH_COUNT * ADC_SCAN_TIMES];

bool bsp_adc_init(void)
{
    return HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_dma_buf,
                             ADC_CH_COUNT * ADC_SCAN_TIMES) == HAL_OK;
}

/* 每通道取 4 轮平均，抑制瞬时噪声；拷贝输出避免读到半更新数据 */
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

配置（Src/board_periph.c）：扫描 + 连续转换 + DMA2 Stream0 循环，
采样时间 56 周期；4 个通道映射 ADC1_IN10..13（PC0-PC3）。

### 2.2 物理量换算

- **电池电压**：ADC 引脚电压经分压电阻采样，读值还原真实电压：

```c
s.battery_mv = ma_push(&f_batt,
    (int32_t)(bsp_adc_raw_to_mv(raw[ADC_CH_BATTERY]) * 100u / BATTERY_DIVIDER_X100));
```

`bsp_adc_raw_to_mv = raw × 3300 / 4095`；`BATTERY_DIVIDER_X100 = 50`
表示 1/2 分压，还原即 ×2。

- **电流采样**：4-20mA 回路经采样电阻转电压后进 ADC，采集值为电压 mV
  （存 `current_mv`），现场按实际采样电阻在上位机换算电流。
- **SHT30 温湿度**（dev_sht30.c）：发测量命令 0x2C06 → 读 6 字节 →
  CRC-8 校验 → 定点换算：

```c
/* T = -45 + 175 * SrT / 65535，x1000 定点；int64 防中间值溢出 */
int64_t t_num = (int64_t)t_raw * 175000 - 45LL * 65536 * 1000;
out->temp_x1000 = (int32_t)(t_num / 65536);
/* RH = 100 * SrH / 65535，uint64 防溢出 */
out->humi_x1000 = (uint32_t)((uint64_t)h_raw * 100000u / 65536u);
```

边界验证：raw=0 → -45.000°C；raw=65535 → 约 +130.006°C，覆盖 SHT30 全量程。

### 2.3 滤波（util_filter.c 滑动均值）

每个物理量一个 `moving_avg_t` 实例（电池/电流 8 点、温湿度 5 点、模拟量
4 点），增量维护窗口和避免每轮重算：

```c
int32_t ma_push(moving_avg_t *f, int32_t sample)
{
    if (f->cnt < f->win) {            /* 未满窗：直接追加 */
        f->window[f->idx] = sample;
        f->sum += sample;
        f->cnt++;
    } else {                          /* 满窗：覆盖最旧样本 */
        f->sum -= f->window[f->idx];
        f->window[f->idx] = sample;
        f->sum += sample;
    }
    f->idx = (f->idx + 1) % f->win;   /* 环形游标 */
    return f->sum / f->cnt;           /* 返回当前均值 */
}
```

### 2.4 一轮采集的组装（task_collect.c）

```c
collect_sample_t s = {0};
uint16_t raw[ADC_CH_COUNT];
bsp_adc_snapshot(raw);
/* 电池/模拟量/电流：raw -> 物理量 -> 滤波，同一份快照保证时间一致 */
...
sht30_sample_t t;
if (sht30_read(&t) && t.valid) {     /* I2C 短超时，失败不阻塞任务 */
    s.temp_x1000 = ma_push(&f_temp, t.temp_x1000);
    s.humi_x1000 = (uint32_t)ma_push(&f_humi, (int32_t)t.humi_x1000);
    s.valid_bits |= PARAM_VALID_BITS_TEMP | PARAM_VALID_BITS_HUMI;
}
bool time_ok = bsp_rtc_get(&s.ts);   /* RTC 时间戳 */
s.valid_bits |= time_ok ? VALID_BITS_TIME : 0;

app_update_sample(&s);               /* 快照到共享状态(comm/alarm 读) */
xQueueSend(app_data_queue(), &s, 0); /* 投递显示队列(非阻塞，满则丢) */
```

`valid_bits` 设计：每一路数据带独立有效位，SHT30 挂掉时温湿度位清零，
其余通道照常采集，下游按位判断，不因单路故障丢弃整帧。

### 2.5 历史日志周期落盘

```c
#define LOG_EVERY_N 10   /* 每 10 个采样点(5s)一条，避免 500ms 高频擦写 */
if ((cycle % LOG_EVERY_N) == 0) {
    w25q_log_rec_t rec = { .epoch = dt_to_epoch(&s.ts), ..., 
                           .alarm_bits = app_get_alarm_bits() };
    w25q_log_append(&rec);
}
```

日志带**当时的报警位快照**，事后可回溯"某时刻设备处于什么报警状态"。

---

## 3. 显示功能（task_display + dev_oled）

### 业务逻辑

200ms 周期渲染当前页面；消费队列只保留最新样本（队列满说明显示跟不上，
直接丢弃中间帧防止滞后累积）；渲染分"改帧缓冲"与"整屏 flush"两阶段。

### 3.1 帧缓冲 + 整屏刷新（dev_oled.c）

```c
static uint8_t g_fb[OLED_PAGES][OLED_WIDTH];  /* 1024B，bit=1 点亮 */

bool oled_flush(void)
{
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        /* 先发页地址命令(B0+page, 00, 10)，再发 1+128 字节数据块 */
        ...
        buf[0] = CTRL_DATA;  /* 0x40：Co=0,D/C=1 */
        memcpy(&buf[1], g_fb[page], OLED_WIDTH);
        bsp_i2c_write(OLED_ADDR, buf, sizeof(buf), 50);
    }
}
```

好处：画面先完整写缓冲再一次性刷，避免逐字 I2C 慢速导致撕裂；每次 flush
8 个 page × 129B 共 8 次 I2C 事务。

### 3.2 字体渲染

5×7 ASCII 列式字模（如 `'A' = {0x7E,0x11,0x11,0x11,0x7E}`），bit0=最上行；
未收录字符画空心框（现场排查乱码来源）；步进 6 像素排版，超宽截断：

```c
uint8_t oled_puts(uint8_t x, uint8_t y, const char *s)
{
    while (*s && x + 6 <= OLED_WIDTH) {
        oled_putc(x, y, *s++);
        x = (uint8_t)(x + 6);   /* 5 列 + 1 间隔 */
    }
    return x;
}
```

### 3.3 四个页面逐页讲解（task_display.c）

**主界面 PAGE_MAIN**：T/H/BAT 三行 + 传感器故障提示：

```c
fmt_temp10(s->temp_x1000, line, sizeof(line));  /* x1000 -> "23.4" 字符串 */
...
if (!(s->valid_bits & PARAM_VALID_BITS_TEMP))
    oled_puts(0, 44, "SENS ERR");   /* 现场可直接定位 SHT30 问题 */
```

`fmt_temp10` 处理负号：`-12345 -> "-12.3"`（符号位与绝对值分开格式化）。

**时间页 PAGE_TIME**：按 `valid_bits` 显示 RTC 时间或 "RTC ERR"。

**参数页 PAGE_PARAM**：5 行参数（温度上限/下限/电池阈值/从机地址/波特率）
+ 光标短横指示当前编辑字段：

```c
const uint8_t row_y[5] = {12, 22, 32, 42, 52};
...
oled_hline(0, row_y[field] + 6, 6);  /* 选中行左侧 6 像素短横 */
```

**状态页 PAGE_STATUS**：报警位 → 文字行错开坐标显示；LOG 条数来自
RTC 备份寄存器（**只读寄存器，无 SPI 开销**，200ms 周期刷屏无压力）。

### 3.4 刷新调度

```c
while (xQueueReceive(app_data_queue(), &s, 0) == pdTRUE)
    got = true;                      /* 清空队列，保留最后一帧 */
if (!got)
    app_get_last_sample(&s);         /* 无新帧则显示最近快照 */
```

用 `vTaskDelayUntil` 严格 200ms 周期，无累积漂移。

---

## 4. 按键与参数配置（task_key）

### 业务逻辑

三键：OK/UP/DOWN。非参数页循环切页；参数页 UP/DOWN 修改当前字段即时
落盘，OK 移动光标。EXTI 只做边沿记录，任务侧去抖。

### 4.1 中断-任务分层（bsp_gpio.c）

```c
static volatile uint8_t g_key_edge;  /* bit i = 键 i 出现过边沿 */

/* ISR 中只置位，零业务 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    for (uint8_t i = 0; i < KEY_ID_MAX; i++)
        if (g_key_pins[i].pin == GPIO_Pin) { g_key_edge |= 1u << i; break; }
}

/* 任务侧原子消费边沿 */
bool bsp_key_take_edge(key_id_t id)
{
    taskENTER_CRITICAL();            /* 与 ISR 互斥的读-改-写 */
    bool had = (g_key_edge & bit) != 0;
    g_key_edge &= ~bit;
    taskEXIT_CRITICAL();
    return had;
}
```

### 4.2 去抖与页面逻辑（task_key.c）

```c
for (int k = 0; k < KEY_ID_MAX; k++) {
    if (!bsp_key_take_edge(k)) continue;
    if (now - last_ms[k] < KEY_DEBOUNCE_MS) continue;  /* 30ms 窗口抑制抖动 */
    last_ms[k] = now;
    if (!bsp_key_pressed(k)) continue;                 /* 边沿后电平确认 */
    app_mark_activity();           /* 任何按键都刷新活动时间，抑制休眠 */

    disp_page_t page = app_get_page();
    if (page != PAGE_PARAM) {
        /* OK 下一页，UP 上一页，DOWN 下一页 */
        ...
        continue;
    }
    /* ---- 参数页 ---- */
    uint8_t field = app_get_param_field();   /* 0..4 五个可编辑字段 */
    device_params_t p;
    app_get_params(&p);
    if (up || down) {
        /* 按字段步进：温度 ±0.5°C(x10 单位 ±5)、电池 ±50mV、地址 ±1、波特率码 ±1 */
        ...
        if (changed) app_set_params(&p);     /* 改一次落一次 Flash */
    } else if (ok) {
        app_set_param_field((field + 1) % PARAM_FIELD_COUNT);  /* 光标前进 */
        app_set_params(&p);
    }
}
```

### 4.3 参数编辑约束

- 电池阈值限 2500..5000mV、从机地址限 1..247、波特率码限 0..4，越界不改。
- `app_set_params` 内部：锁内更新内存 → 锁外 `w25q_save_params` 落盘
  （避免 SPI 擦写约 160ms 占用互斥锁阻塞显示/通讯读取）。
- 按键侧与 Modbus 侧共享同一套参数校验与落盘路径，两边改参数行为一致。

---

## 5. Modbus 通讯（task_comm + dev_modbus + bsp_uart）

### 业务逻辑

RS485 从机：上位机读遥测（0x04）、读/写参数（0x03/0x06/0x10）、写日志游标
取历史、写时间寄存器校时。收帧不轮询，全程 DMA+IDLE 中断驱动。

### 5.1 收帧链路（bsp_uart.c）

```
RS485 总线 ──> USART2 RX(PD6)
   └─> DMA1 Stream5 把字节搬进 g_rs485_dma_rx[256]（NORMAL 模式）
   └─> 总线静默(>1 字节时间) ──> USART2 IDLE 中断
        └─> HAL_UARTEx_RxEventCallback(huart, size)：
             1) rb_write(ringbuf, dma_buf, size)   // 一帧落缓冲
             2) xSemaphoreGiveFromISR(帧信号量)     // 唤醒 comm 任务
             3) HAL_UARTEx_ReceiveToIdle_DMA(...)  // 重新挂接，准备下一帧
```

为什么 DMA 用 NORMAL 而非 CIRCULAR：IDLE 中断发生时 HAL 已停止 DMA，
回调里拿到"本次帧长"后重新挂接即可；若用 CIRCULAR，帧长要靠剩余计数反推，
易在回绕边界出错。

**错误恢复**：溢出/噪声/帧错误触发 `HAL_UART_ErrorCallback` → 停 DMA →
清 ringbuf → 重挂接收，防止错误后 DMA 永久卡死。

### 5.2 comm 任务主循环（task_comm.c）

```c
for (;;) {
    if (xSemaphoreTake(bsp_485_frame_sem(), pdMS_TO_TICKS(1000)) == pdTRUE) {
        uint16_t n = rb_read(bsp_485_rxbuf(), req, sizeof(req));
        uint16_t off = 0;
        /* 一次唤醒可能已缓冲多帧(主机连发)，逐帧解析防丢 */
        while (n > 0 && off < n) {
            uint16_t fl = modbus_frame_len(&req[off], n - off);
            if (fl == 0) break;             /* 不足一帧/CRC 坏，丢弃剩余 */
            ...
            uint16_t rn = modbus_process_frame(&cfg, &req[off], fl, resp, sizeof(resp));
            if (rn > 0) bsp_485_send(resp, rn, 100);
            off += fl;
        }
    }
    bsp_wdg_task_feed(WDG_TASK_COMM);       /* 1s 超时也喂心跳 */
}
```

### 5.3 帧长推断（dev_modbus.c）

Modbus RTU 无固定帧长，按功能码推断：

```c
uint16_t modbus_frame_len(const uint8_t *buf, uint16_t len)
{
    switch (buf[1]) {
    case FC_READ_HOLDING: case FC_READ_INPUT: case FC_WRITE_SINGLE:
        need = 8;                 break;
    case FC_WRITE_MULTI:
        if (len < 7) return 0;
        need = 9 + buf[6];        /* 固定头 9B + byteCnt 个数据 */
        break;
    default:
        need = 8;                 /* 未知功能码按最小请求帧，交协议栈回异常 */
    }
    if (need > len || !crc16_modbus_check(buf, need)) return 0;
    return need;
}
```

### 5.4 协议栈处理流程（dev_modbus.c）

入口 `modbus_process_frame(cfg, req, req_len, resp, resp_cap)`：

1. `req_len < 4` 或 CRC 错 → 返回 0（**丢弃不回**，Modbus 规范要求静默）。
2. `addr != slave_addr` 且非广播 → 返回 0（总线上其他设备帧不打扰）。
3. 按功能码分支：

**0x03/0x04 读寄存器**：
```
校验 req_len==8、数量 1..125
-> 逐寄存器调回调 cb(reg+i, &v)，任一地址非法 -> 回异常 0x02
-> 组装 [addr|fc|byteCnt|data...|CRC] 返回
```

**0x06 写单个**：
```
校验 req_len==8 -> 回调写 -> 失败回异常 0x02
-> 广播: 执行但不响应(返回 0)；非广播: 原样回显 6 字节 + 新 CRC
```

**0x10 写多个**：
```
校验 byteCnt == qty*2、帧长匹配 -> 循环逐寄存器写
-> 任一失败回异常 0x02（注意：已写的寄存器不回滚，从机单寄存器语义）
-> 非广播回 [addr|fc|reg|qty|CRC]
```

**未知功能码** → 异常帧 `[addr | fc|0x80 | 0x01 | CRC]`。

### 5.5 寄存器读写回调（task_comm.c）

三个回调把协议栈与业务解耦：

```c
static bool mb_read_input(uint16_t reg, uint16_t *out)
{
    if (reg <= 9) {                      /* 遥测：取共享状态快照 */
        collect_sample_t s;
        bool has = app_get_last_sample(&s);   /* 无数据返回全 0 */
        switch (reg) {
        case 0: *out = (uint16_t)(s.temp_x1000 / 100); break;   /* x10 */
        case 2: *out = (uint16_t)(s.battery_mv > 0xFFFF ? 0xFFFF : s.battery_mv); break;
        ...
        }
        return true;
    }
    if (reg == 10) { *out = (uint16_t)w25q_log_count(); return true; }
    if (reg >= 11 && reg <= 19) {        /* 游标预取的历史记录 */
        if (!g_rec_valid) { *out = 0; return true; }
        /* 从 g_rec 按偏移取字段(epoch 拆高低 16 位等) */
    }
    return false;                        /* 非法地址 -> 协议栈回异常 */
}
```

写回调 `mb_write_holding` 的要点：
- **校时两段式**：0x10-0x13 写入暂存区 `g_time_stage`（不立即生效），
  0x14 写 1 才 `bsp_rtc_set(&g_time_stage)` 一次性应用——避免写一半主机
  断电造成日期/时间不同步。
- **日志游标**（reg 6）：写入即 `prefetch_cursor(val)` 读 Flash 预取该记录，
  随后读 11-19 寄存器直接出数据，读多次不再碰 SPI。
- **参数写**：校验范围 → `app_set_params`（内存 + 落盘）。
- **波特率联动**：reg 4 写入后调 `bsp_485_set_baud_code(p.baud_code)`：
  停接收 → 改 BRR → `HAL_UART_Init` → 重挂 DMA。约定上位机写完波特率码后
  切到新波特率等响应。

### 5.6 响应发送时序（bsp_uart.c）

```c
bool bsp_485_send(const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    RS485_SET_TX();                       /* DE 拉高 -> 收发器进发送态 */
    HAL_StatusTypeDef st = HAL_UART_Transmit(&huart_485, data, len, timeout_ms);
    /* 轮询 TC 标志：移位寄存器发空才算真正发完。
       用 HAL_GetTick 计时，自旋递减太快会提前切向截断末字节 */
    uint32_t t0 = HAL_GetTick();
    while (__HAL_UART_GET_FLAG(&huart_485, UART_FLAG_TC) == RESET)
        if ((HAL_GetTick() - t0) > timeout_ms) break;
    RS485_SET_RX();                       /* DE 拉低 -> 回接收态 */
    return st == HAL_OK;
}
```

`HAL_UART_Transmit` 返回时最后一字节刚写入 TDR，移位寄存器仍在发送；
必须等 TC 才切回 RX，否则响应末字节被截断导致主机 CRC 错。

---

## 6. 报警功能（task_alarm）

### 业务逻辑

100ms 周期：读最新采样与参数 → 阈值判定 → 更新报警位 → 非静音时驱动蜂鸣
间歇鸣响 → 兼任看门狗监督点。

### 6.1 阈值判定（task_alarm.c）

```c
uint8_t bits = 0;
if (have && (s.valid_bits & PARAM_VALID_BITS_TEMP)) {
    sht_fail_cnt = 0;
    int16_t temp_x10 = (int16_t)(s.temp_x1000 / 100);   /* x1000 -> x10 */
    if (temp_x10 > p.temp_alarm_hi) bits |= ALM_BIT_TEMP_HI;
    if (temp_x10 < p.temp_alarm_lo) bits |= ALM_BIT_TEMP_LO;
} else if (sht_fail_cnt < UINT32_MAX) {
    sht_fail_cnt++;
}
if (sht_fail_cnt >= 30) bits |= ALM_BIT_SENSOR;  /* 连续 3s 无有效数据=传感器故障 */
if (have && (s.valid_bits & PARAM_VALID_BITS_BATT))
    if (s.battery_mv > 0 && s.battery_mv < p.batt_alarm_mv)
        bits |= ALM_BIT_BATT_LO;
app_set_alarm_bits(bits);              /* 全量覆盖写 */
```

细节：温度无效时**不判**高低温（避免用旧值误报），但累计失败次数，
连续 30 个周期（3s）置传感器故障位；`battery_mv > 0` 防初始零值误报欠压。

### 6.2 蜂鸣相位（间歇鸣响更省电且醒目）

```c
bool alarming = (bits != 0) && !p.mute;
if (alarming) {
    uint32_t now = HAL_GetTick();
    if (now - last_toggle >= (buzz_phase ? BUZZ_ON_MS : BUZZ_OFF_MS)) {
        buzz_phase = !buzz_phase;       /* 鸣 120ms / 停 400ms 交替 */
        last_toggle = now;
        bsp_pwm_beep(buzz_phase);
    }
} else {
    bsp_pwm_beep(false);                /* mute 或报警解除立即静音 */
    buzz_phase = false;
}
```

`mute` 可经 Modbus 保持寄存器 5 或按键消音（当前按键路径未挂 mute，见进度）。

### 6.3 兼任看门狗监督（见第 8 节）

---

## 7. 掉电存储（dev_w25q）

### 业务逻辑

W25Q128 划分两块：**参数区**（sector 0，设备配置，写频繁低）与**日志区**
（sector 16 起 8KB，环形追加 256 条历史）。NOR Flash 铁律：写前擦除
（只能 1→0）、按 4KB 扇区擦、按 256B 页编程、不可跨页。

### 7.1 底层时序

```c
/* 页编程：命令+地址+数据必须在一段 CS 内完成（bsp_spi_xfer_flash 保证） */
uint8_t hdr[4] = {CMD_PAGE_PROGRAM, a16, a8, a0};
if (!bsp_spi_xfer_flash(hdr, 4, data, NULL, len, TIMEOUT_WRITE_MS)) return false;
return w25q_wait_busy(TIMEOUT_WRITE_MS);   /* 轮询 BUSY 位直到写入完成 */
```

`w25q_wait_busy` 循环读状态寄存器 S1 的 BUSY 位，扇区擦除给 400ms 预算
（典型 150ms）。写使能（0x06）后必须紧跟编程/擦除命令，否则 WEL 位自动
清除。

### 7.2 参数保存与恢复

```
Flash 布局(sector 0)：
[magic 0xCD78 (2B) | seq (2B) | len (2B) | crc16 (2B)] + payload
```

**保存**：读旧头部取 seq+1 → 打包 → 擦扇区 → 页编程。序号让上位机可识别
保存次数（掉电写坏时可判断新旧）。

**恢复**（app_state.c 启动时）：

```c
g_params = PARAM_DEFAULT;
uint16_t n = w25q_load_params((uint8_t *)&g_params, sizeof(g_params));
if (n != sizeof(g_params)) {            /* 无有效数据(首刷/损坏) */
    g_params = PARAM_DEFAULT;
    w25q_save_params(...);              /* 立即写入默认值，下次直接命中 */
}
/* 载入后合法性钳制：防 Flash 脏数据导致越界 */
if (g_params.modbus_addr == 0 || g_params.modbus_addr > 247)
    g_params.modbus_addr = PARAM_DEFAULT.modbus_addr;
if (g_params.baud_code > 4) g_params.baud_code = 0;
```

`w25q_load_params` 内部三重校验：magic → len 范围 → CRC16，任一失败
返回 0，上层回退默认值。

### 7.3 日志环形存储

```
日志区布局：256 槽 × 32B = 8KB（2 扇区）
单槽：[magic 0x4C47 (2B) | crc16 (2B) | w25q_log_rec_t (≤24B) | 0xFF 填充]
写入指针：RTC 备份寄存器 BKP_DR1（VBAT 供电下断电不丢）
```

**追加**：

```c
uint32_t total = log_total_read();
uint32_t slot = total % W25Q_LOG_CAPACITY;
if (slot == 0 && total != 0) {           /* 写满一轮回绕 */
    for (s = 0; s < LOG_SECTORS; s++)
        w25q_sector_erase(LOG_BASE_ADDR + s * 4096);
    total = 0;                           /* 计数清零！否则读接口按旧 total
                                            回推的槽位全是 FF */
}
/* 组装 32B 槽(FF 预填充) -> 单页编程(32 整除 256 不跨页) */
w25q_page_write(log_slot_addr(slot), buf, 32);
log_total_write(total + 1);
```

**读取 idx=0 为最新**：`slot = (total-1-idx) % 256`，逐槽校验 magic+CRC，
坏槽返回 false（上位机可跳过继续查更早记录）。

### 7.4 与任务的关系

- 参数写发生在 comm/key 任务（偶发，约 160ms 擦写阻塞该任务，可接受；
  报警任务优先级更高会抢占，IWDG 2s 窗口内不受影响）。
- 日志写发生在 collect 任务 5s 一次；`w25q_log_count()`（状态页显示、
  Modbus 寄存器 10）只读备份寄存器，**零 SPI 开销**，200ms 刷屏无压力。

---

## 8. 看门狗监督（bsp_wdg）

### 业务逻辑

传统"每任务各自喂狗"的缺陷：任一任务卡死但只要 idle 或别的任务还在喂，
系统就带病运行。本设计改为**单一监督点 + 多任务心跳**。

### 实现

```c
static volatile uint32_t g_heartbeat[WDG_TASK_NUM];  /* 各任务最近心跳时刻 */

void bsp_wdg_task_feed(wdg_task_id_t id) { g_heartbeat[id] = HAL_GetTick(); }

bool bsp_wdg_poll(uint32_t task_timeout_ms)   /* alarm 任务每 100ms 调 */
{
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < WDG_TASK_NUM; i++)
        if ((uint32_t)(now - g_heartbeat[i]) > task_timeout_ms)
            return false;      /* 有任务卡死：故意不喂 -> IWDG 超时复位 */
    HAL_IWDG_Refresh(&g_hiwdg);
    return true;
}
```

- 心跳时间戳差值比较用无符号减法，`HAL_GetTick` 回绕（49.7 天）也正确。
- 5 个业务任务各自喂心跳，alarm 任务统一 poll：**全部**健康才真正喂 IWDG。
- IWDG 参数：LSI 32kHz / 预分频 64 = 500Hz 计数，重装 1500 ≈ 3s 超时；
  任务心跳超时窗口 2s < 3s，卡死检测先于复位动作。
- 任务被互斥锁死锁、被从机拉死总线、栈溢出跑飞——无论哪种卡法，
  心跳一停系统必复位，不留"假活"状态。

---

## 9. 低功耗 STOP（main.c 空闲钩子）

### 业务逻辑

电池供电设备静置时应休眠：仅当系统"闲下来"（所有任务阻塞）且满足条件时，
在 idle 钩子里进入 STOP；靠 RTC 唤醒定时器周期唤醒喂狗 + 按键随时唤醒。

### 9.1 进入条件

```c
static bool sleep_allowed(void)
{
    device_params_t p;
    app_get_params(&p);
    return p.sleep_en                /* 参数区开关(默认关，现场按需开) */
        && app_get_alarm_bits() == 0 /* 报警期间不休眠 */
        && app_ms_since_activity() > SLEEP_IDLE_MS;  /* 30s 无按键/通讯 */
}
```

活动时间戳由按键任务与 comm 任务（收到帧时）调用 `app_mark_activity()`
刷新——Modbus 查询期间设备不会睡着。

### 9.2 休眠与唤醒

```c
void vApplicationIdleHook(void)
{
    if (!sleep_allowed()) { __WFI(); return; }   /* 不休眠也轻度等中断 */

    HAL_SuspendTick();                          /* 关 SysTick 中断 */
    /* 1s 周期唤醒：ck_spre=1Hz(LSL/128/256)，计数器 0 => 下一秒触发 */
    HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, 0, RTC_WAKEUPCLOCK_CK_SPRE_17BITS);
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    /* ---- 唤醒后 ---- */
    SystemClock_Config();                       /* 时钟回 HSI，重建 168MHz */
    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
    HAL_ResumeTick();
    (void)bsp_adc_init();                       /* STOP 期间 DMA 被停，重启 */
    (void)bsp_485_init();
    bsp_wdg_force_refresh();                    /* 休眠期间任务冻结属正常，
                                                   强制刷新心跳并喂狗 */
}
```

核心矛盾与解法：**IWDG 由 LSI 驱动，STOP 下不停表且无法软件暂停**，
故必须周期性醒来喂狗；唤醒周期 1s 远小于 3s 超时，留足 LSI 精度余量
（LSI 误差可达 ±50%）。休眠期间任务全部冻结，唤醒后强制刷新所有心跳
防止 poll 误判卡死。

---

## 10. RTC 与校时

### 10.1 时间源与初始化（bsp_rtc.c）

- LSI 32kHz 经 128×256 分频得 1Hz 日历时钟；24h 制、二进制格式。
- 备份域 `BKP_DR0` 存 0xA55A 初始化标志：掉电首启（备份域无电）设默认
  时间 2026-01-01，避免 1970 引发 `dt_to_epoch` 越界。
- 读时间必须 `HAL_RTC_GetTime` 后**紧跟** `HAL_RTC_GetDate`（硬件锁存
  机制），否则日期与时间不同步。

### 10.2 校时业务流（Modbus 两段式）

```
主机: 0x10 写 reg 0x10..0x13 (年/月日/时分/秒)   -> 从机暂存 g_time_stage
主机: 0x06 写 reg 0x14 = 1                      -> bsp_rtc_set 一次性应用
```

`bsp_rtc_set` 先 `dt_is_valid` 校验（年月日/时分秒/闰年二月），非法拒绝
返回异常，避免把 RTC 写成无意义日期。应用后采集任务下一轮自动带上新
时间戳，历史日志 epoch 随之正确。

---

## 11. 工具库实现

### 11.1 util_ringbuf（无锁单生产者/单消费者）

- 容量必须 2 的幂，掩码取模免除法（ISR 友好）。
- **预留 1 字节**区分空/满：`free = size - 1 - available`，可用容量
  = size-1，避免 head==tail 语义二义性。
- 写满**静默丢新数据**（`rb_write` 返回实际写入数），串口场景宁丢新帧
  不破坏已收帧；读侧只动 tail、写侧只动 head，ISR/任务各占一侧无锁安全。

```c
uint16_t rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len)
{
    uint16_t head = rb->head;              /* 本地副本，最后统一写回 */
    while (written < len) {
        uint16_t next = (head + 1) & rb->mask;
        if (next == rb->tail) break;       /* 满 */
        rb->buf[head] = data[written++];
        head = next;
    }
    rb->head = head;                       /* 数据写完才更新，读者永远看不到半帧 */
}
```

### 11.2 util_filter（四种滤波）

| 函数 | 场景 | 说明 |
|------|------|------|
| `ma_push` 滑动均值 | 常规 ADC/SHT30 | O(1) 增量维护 sum，窗口 4/5/8 |
| `median_filter` | 脉冲毛刺 | 拷贝排序取中值，n≤16 |
| `trimmed_mean` | 去极值均值 | 去掉最大最小各 1 个再平均，n≥3 |
| `lowpass1` | 惯性大的量 | `y = (a·x + (1024-a)·y_prev)/1024`，系数定点 1024 |

工程当前采集链路用滑动均值；中值/去极值/低通为备选（面试展示点）。

### 11.3 util_crc（CRC16-Modbus）

多项式 0xA001（反射 0x8005），初值 0xFFFF，逐位查表等价实现（逐位版，
代码小）；输出**低字节在前**——Modbus 传输约定，与以太网相反：

```c
frame[payload_len]     = (uint8_t)(crc & 0xFF);
frame[payload_len + 1] = (uint8_t)((crc >> 8) & 0xFF);
```

### 11.4 util_time

- `dt_to_epoch`：2000-01-01 起算秒数（非 UNIX 纪元），逐月累加天数，
  年范围 2000-2099（RTC 硬件字段宽度决定）。
- `dt_format`：无 `snprintf` 依赖的手写 u32 转字符串（嵌入式节约栈/代码，
  且 nosys 环境可控），输出 "YYYY-MM-DD HH:MM:SS"。

---

## 12. 修复记录与进度

### 12.1 审查修复清单（16 项）

| # | 问题 | 根因 | 修复 |
|---|------|------|------|
| 1 | HAL_GetTick 冻结/HAL_Delay 死循环（致命） | FreeRTOS 接管 SysTick 无人喂 HAL 时基 | SysTick_Handler 分发：HAL_IncTick + FreeRTOS tick |
| 2 | W25Q 读写无效（致命） | 命令与数据分两次传输，CS 中途拉高终止命令 | bsp_spi_xfer_flash 单段 CS |
| 3 | ADC 通道错误（致命） | 采 IN0-3(PA0-3 被占)，模拟脚配 PC0-3 | ADC_CHANNEL_10..13 |
| 4 | 485 末字节截断 | TC 自旋等待无延时提前超时 | HAL_GetTick 计时 |
| 5 | comm 栈溢出风险 | req/resp 256B×2 + 落盘缓冲 256B > 1KB | 栈 512 words |
| 6 | 日志回绕读不出 | 擦除后计数未复位，槽位按旧计数回推 | 擦除后计数清零 |
| 7 | 波特率码不生效 | 只存参数未重配 USART2 | bsp_485_set_baud_code |
| 8 | 粘包丢帧 | 一次唤醒只处理首帧 | modbus_frame_len 循环解析 |
| 9 | 蜂鸣占空比失真 | 用 Init.Period 而 ARR 已变 | 取实际 ARR |
| 10 | 传感器故障无报警 | ALM_BIT_SENSOR 未使用 | 连续 3s 置位 |
| 11 | 电流字段语义不符 | current_ua_x10 实存 mV | 改名 current_mv |
| 12 | 电池电压两次快照 | 调 bsp_adc_battery_mv 内部二次快照 | 同一份快照 |
| 13 | 状态页行覆盖 | TEMP HI/LO 同坐标 | 错开 + 补 SENS |
| 14 | SHT30 换算溢出 | int32/uint32 中间值超范围 | int64/uint64 |
| 15 | 启动文件不兼容 | IAR 语法 + GCC 工具链 | ST 官方 GCC 版 |
| 16 | 互斥锁链接失败 | configUSE_MUTEXES 未开 | 显式开启 |

### 12.2 进度

- ✅ 全部代码完成，`make` 零警告零错误，产物 Flash 71KB / RAM 24KB。
- ⏳ 待硬件联调：按实际板子核对 board_config.h（分压比/I2C 地址/蜂鸣
  频率/DE-RE 并联）后烧录整机验证。
- ⬜ 拓展选做：CAN2.0A 上报、SDIO 日志、FSMC LCD、RTC 闹钟唤醒、OTA。
- ⬜ 可选加固：参数区双备份扇区、按键消音(mute)入口、日志写保护。
