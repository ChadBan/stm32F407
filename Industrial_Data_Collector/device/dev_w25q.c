/**
 * @file    dev_w25q.c
 * @brief   W25Q128 驱动与参数存储实现
 */
#include "dev_w25q.h"
#include "bsp_spi.h"
#include "bsp_uart.h"
#include "util_crc.h"
#include "stm32f4xx.h"
#include <string.h>

/* Flash 底层调试打印开关：置 1 输出 wb:/sv: 等擦写过程信息(正常运行时每 5s
   刷屏)，置 0 静默。排查 Flash 问题时临时置 1 即可。 */
#define W25Q_DEBUG 0

#if W25Q_DEBUG
#define W25Q_DBG(...) bsp_dbg_printf(__VA_ARGS__)
#else
#define W25Q_DBG(...) ((void)0)
#endif

/* W25Q 指令集 */
#define CMD_WRITE_ENABLE 0x06
#define CMD_READ_STATUS 0x05
#define CMD_PAGE_PROGRAM 0x02
#define CMD_SECTOR_ERASE 0x20 /* 4KB 扇区擦除(0xD8 是 64KB 块擦除，别混用) */
#define CMD_READ_DATA 0x03
#define CMD_JEDEC_ID 0x9F

#define TIMEOUT_OP_MS 10
#define TIMEOUT_ERASE_MS 600 /* 4KB 扇区擦除典型 ~45ms，最大 ~400ms，留足余量 */
#define TIMEOUT_WRITE_MS 10

static bool w25q_write_enable(void)
{
    uint8_t cmd = CMD_WRITE_ENABLE;
    return bsp_spi_xfer(&cmd, NULL, 1, TIMEOUT_OP_MS);
}

/* DWT 周期计数忙等：调度器启动前 FreeRTOS 临界区会临时屏蔽 SysTick
   (BASEPRI=0x50，port.c 哨兵计数 0xaaaaaaaa 使 BASEPRI 在 xPortStartScheduler
   之前不会被恢复)，此时 HAL_Delay/HAL_GetTick 全部冻结，必须改用不依赖
   中断的 DWT 硬件计数器。 */
static void w25q_dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    __DSB();
}

static bool w25q_wait_busy(uint32_t timeout_ms)
{
    uint8_t tx[2] = {CMD_READ_STATUS, 0x00};
    uint8_t rx[2] = {0, 0};
    w25q_dwt_init();
    uint32_t t0 = DWT->CYCCNT;
    /* 直接用原始周期数比较，避免除法换算引入误差 */
    uint32_t limit = timeout_ms * (SystemCoreClock / 1000u);
    W25Q_DBG("wb: t0=%lu sc=%lu limit=%lu\r\n", t0,
                   (unsigned long)SystemCoreClock, (unsigned long)limit);
    do
    {
        bool ok = bsp_spi_xfer(tx, rx, 2, TIMEOUT_OP_MS);
        W25Q_DBG("wb: ok=%u rx=%02x cnt=%lu\r\n", (unsigned)ok, rx[1],
                       (unsigned long)DWT->CYCCNT);
        if (!ok)
            return false;
        if ((rx[1] & 0x01) == 0)
            return true; /* BUSY 位清零 = 就绪 */
    } while ((DWT->CYCCNT - t0) < limit);
    W25Q_DBG("wb: timeout\r\n");
    return false;
}

bool w25q_read_id(uint32_t *id)
{
    if (id == NULL)
        return false;
    uint8_t tx[4] = {CMD_JEDEC_ID, 0, 0, 0};
    uint8_t rx[4] = {0};
    if (!bsp_spi_xfer(tx, rx, 4, TIMEOUT_OP_MS))
        return false;
    *id = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
    return *id != 0 && *id != 0xFFFFFF;
}

bool w25q_sector_erase(uint32_t addr)
{
    addr &= ~(W25Q_SECTOR_SIZE - 1); /* 对齐扇区 */
    bool ok = w25q_write_enable();
    W25Q_DBG("sv: we=%u\r\n", (unsigned)ok);
    if (!ok)
        return false;
    uint8_t cmd[4] = {CMD_SECTOR_ERASE,
                      (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    ok = bsp_spi_xfer(cmd, NULL, 4, TIMEOUT_OP_MS);
    W25Q_DBG("sv: er=%u\r\n", (unsigned)ok);
    if (!ok)
        return false;
    ok = w25q_wait_busy(TIMEOUT_ERASE_MS);
    W25Q_DBG("sv: wb=%u\r\n", (unsigned)ok);
    return ok;
}

bool w25q_page_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || len > W25Q_PAGE_SIZE)
        return false;
    /* 禁止跨页编程 */
    if ((addr & (W25Q_PAGE_SIZE - 1)) + len > W25Q_PAGE_SIZE)
        return false;
    if (!w25q_write_enable())
        return false;
    /* 命令 + 24bit 地址 + 数据，单段 CS 内完成 */
    uint8_t hdr[4] = {CMD_PAGE_PROGRAM,
                      (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    if (!bsp_spi_xfer_flash(hdr, sizeof(hdr), data, NULL, len, TIMEOUT_WRITE_MS))
        return false;
    return w25q_wait_busy(TIMEOUT_WRITE_MS);
}

bool w25q_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (data == NULL)
        return false;
    uint32_t sent = 0;
    while (sent < len)
    {
        uint32_t cur = addr + sent;
        uint16_t room = (uint16_t)(W25Q_PAGE_SIZE - (cur & (W25Q_PAGE_SIZE - 1)));
        uint16_t chunk = (len - sent < room) ? (uint16_t)(len - sent) : room;
        if (!w25q_page_write(cur, data + sent, chunk))
            return false;
        sent += chunk;
    }
    return true;
}

bool w25q_read(uint32_t addr, uint8_t *data, uint32_t len)
{
    if (data == NULL || len == 0)
        return false;
    uint8_t hdr[4] = {CMD_READ_DATA,
                      (uint8_t)(addr >> 16), (uint8_t)(addr >> 8), (uint8_t)addr};
    /* 命令头 + 读数据须在单段 CS 内完成，否则 CS 上升沿终止读命令 */
    return bsp_spi_xfer_flash(hdr, sizeof(hdr), NULL, data, len, TIMEOUT_OP_MS);
}

/* ============ 参数存储 ============ */
/* 布局：sector0 头部 [magic(2) | seq(2) | len(2) | crc(2)] + payload
   每次保存 seq+1，读取时比较两份备份取有效且序号最大者。为简化此处单份，
   生产可加双 sector 备份。 */
#define PARAM_MAGIC 0xCD78u
#define PARAM_ADDR 0u

bool w25q_save_params(const uint8_t *blob, uint16_t len)
{
    /* payload 后还有 8 字节头 [magic|seq|len|crc]，故 len 上限须留出头部空间，
       否则 memcpy(&packet[8], blob, len) 会越界写 packet[256] 栈缓冲。 */
    if (blob == NULL || len == 0 || len > W25Q_PAGE_SIZE - 8)
        return false;

    uint16_t seq = 0;
    uint8_t hdr[8];
    /* 读旧头部取 seq（首次为 0xFFFF，视为空则 seq=0） */
    W25Q_DBG("sv: read-hdr\r\n");
    bool rd = w25q_read(PARAM_ADDR, hdr, 8);
    W25Q_DBG("sv: rd=%u\r\n", (unsigned)rd);
    if (rd)
    {
        uint16_t magic = (hdr[0] << 8) | hdr[1];
        if (magic == PARAM_MAGIC)
            seq = (uint16_t)((hdr[2] << 8) | hdr[3]) + 1;
    }

    uint8_t packet[W25Q_PAGE_SIZE];
    packet[0] = (uint8_t)(PARAM_MAGIC >> 8);
    packet[1] = (uint8_t)PARAM_MAGIC;
    packet[2] = (uint8_t)(seq >> 8);
    packet[3] = (uint8_t)seq;
    packet[4] = (uint8_t)(len >> 8);
    packet[5] = (uint8_t)len;
    uint16_t crc = crc16_modbus(blob, len);
    packet[6] = (uint8_t)(crc >> 8);
    packet[7] = (uint8_t)crc;
    memcpy(&packet[8], blob, len);

    if (!w25q_sector_erase(PARAM_ADDR))
        return false;
    W25Q_DBG("sv: erased\r\n");
    bool ok = w25q_page_write(PARAM_ADDR, packet, (uint16_t)(8 + len));
    W25Q_DBG("sv: written=%u\r\n", (unsigned)ok);
    return ok;
}

uint16_t w25q_load_params(uint8_t *blob, uint16_t cap)
{
    if (blob == NULL || cap == 0)
        return 0;
    uint8_t hdr[8];
    if (!w25q_read(PARAM_ADDR, hdr, 8))
        return 0;
    uint16_t magic = (hdr[0] << 8) | hdr[1];
    if (magic != PARAM_MAGIC)
        return 0;
    uint16_t len = (hdr[4] << 8) | hdr[5];
    uint16_t crc = (hdr[6] << 8) | hdr[7];
    if (len == 0 || len > cap)
        return 0;
    if (!w25q_read(PARAM_ADDR + 8, blob, len))
        return 0;
    if (crc16_modbus(blob, len) != crc)
        return 0; /* 校验失败，视为无有效数据 */
    return len;
}

/* ============ 历史日志（环形） ============ */
#define LOG_BASE_ADDR (16u * W25Q_SECTOR_SIZE) /* 65536，避开参数区 */
#define LOG_SECTORS 2u                         /* 256*32=8192B = 2 扇区 */
#define LOG_SLOT_SIZE 32u
#define LOG_MAGIC 0x4C47u       /* 'LG' */
#define LOG_PTR_BKP RTC_BKP_DR1 /* 存 total_written */

static uint32_t log_total_read(void)
{
    uint32_t v = HAL_RTCEx_BKUPRead(&hrtc, LOG_PTR_BKP);
    if (v == 0xFFFFFFFFu)
        v = 0; /* 备份域未初始化 */
    return v;
}
static void log_total_write(uint32_t v)
{
    HAL_RTCEx_BKUPWrite(&hrtc, LOG_PTR_BKP, v);
}

static uint32_t log_slot_addr(uint32_t slot)
{
    return LOG_BASE_ADDR + slot * LOG_SLOT_SIZE;
}

bool w25q_log_append(const w25q_log_rec_t *rec)
{
    if (rec == NULL)
        return false;
    uint32_t total = log_total_read();
    uint32_t slot = total % W25Q_LOG_CAPACITY;

    /* 新一轮起点(刚回绕且非首次)：擦除整个日志区，计数器清零。
       注意不能只擦不重置计数——擦除后旧槽全部失效，若 total 继续递增，
       读接口会按 total 回推旧槽位置，读到全是 FF 的无效记录。 */
    if (slot == 0 && total != 0)
    {
        for (uint32_t s = 0; s < LOG_SECTORS; s++)
        {
            if (!w25q_sector_erase(LOG_BASE_ADDR + s * W25Q_SECTOR_SIZE))
                return false;
        }
        total = 0;
    }

    /* 组装 32B 槽：[magic(2)|crc(2)|payload(sizeof(rec))|pad] */
    uint8_t buf[LOG_SLOT_SIZE];
    memset(buf, 0xFF, sizeof(buf));
    uint16_t crc = crc16_modbus((const uint8_t *)rec, sizeof(*rec));
    buf[0] = (uint8_t)(LOG_MAGIC >> 8);
    buf[1] = (uint8_t)LOG_MAGIC;
    buf[2] = (uint8_t)(crc >> 8);
    buf[3] = (uint8_t)crc;
    memcpy(&buf[4], rec, sizeof(*rec));

    /* 槽按 32B 对齐，不跨页边界(32 整除 256)，可单页写 */
    if (!w25q_page_write(log_slot_addr(slot), buf, LOG_SLOT_SIZE))
        return false;
    log_total_write(total + 1);
    return true;
}

bool w25q_log_read(uint32_t idx, w25q_log_rec_t *out)
{
    if (out == NULL)
        return false;
    uint32_t total = log_total_read();
    uint32_t count = (total < W25Q_LOG_CAPACITY) ? total : W25Q_LOG_CAPACITY;
    if (idx >= count)
        return false;

    /* idx=0 为最新：物理槽 = (total-1-idx) mod CAP */
    uint32_t slot = (uint32_t)((total - 1 - idx) % W25Q_LOG_CAPACITY);
    uint8_t buf[LOG_SLOT_SIZE];
    if (!w25q_read(log_slot_addr(slot), buf, LOG_SLOT_SIZE))
        return false;

    uint16_t magic = (buf[0] << 8) | buf[1];
    uint16_t crc = (buf[2] << 8) | buf[3];
    if (magic != LOG_MAGIC)
        return false;
    memcpy(out, &buf[4], sizeof(*out));
    return crc16_modbus((const uint8_t *)out, sizeof(*out)) == crc;
}

uint32_t w25q_log_count(void)
{
    uint32_t total = log_total_read();
    return (total < W25Q_LOG_CAPACITY) ? total : W25Q_LOG_CAPACITY;
}
