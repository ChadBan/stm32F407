/**
 * @file    dev_w25q.h
 * @brief   W25Q128 SPI NOR Flash 驱动 + 掉电存储（参数区 / 日志区）
 *
 * NOR Flash 规则：写前必须擦除（只能 1->0），按扇区(4KB)擦除、按页(256B)编程。
 * 上层封装两块区域：
 *   - 参数区：设备地址、报警阈值等，带 CRC + 序号，双备份防止写坏。
 *   - 日志区：历史采集记录环形追加。
 */
#ifndef DEV_W25Q_H
#define DEV_W25Q_H

#include "board_config.h"

#define W25Q_SECTOR_SIZE 4096u
#define W25Q_PAGE_SIZE 256u

/* ---- 底层原语 ---- */
bool w25q_read_id(uint32_t *id);
bool w25q_sector_erase(uint32_t addr);
bool w25q_page_write(uint32_t addr, const uint8_t *data, uint16_t len); /* len<=256 */
bool w25q_write(uint32_t addr, const uint8_t *data, uint32_t len);      /* 自动跨页 */
bool w25q_read(uint32_t addr, uint8_t *data, uint32_t len);

/* ---- 参数存储（结构体由 app 层定义，此处按字节 + CRC + magic 封装） ---- */
/* 写入 blob（含内部 CRC 校验），返回 true 成功 */
bool w25q_save_params(const uint8_t *blob, uint16_t len);
/* 读取最近有效 blob，返回读取长度，0 表示无有效数据 */
uint16_t w25q_load_params(uint8_t *blob, uint16_t cap);

/* ============ 历史日志（环形追加，容量 W25Q_LOG_CAPACITY 条） ============ */
/* 单条记录定长 32B，自带 magic + CRC，可独立校验有效性。
   写入指针存于 RTC 备份寄存器，掉电不丢失且免去每写擦除。 */
#define W25Q_LOG_CAPACITY 256u

typedef struct
{
    uint32_t epoch;       /* 采集时间戳(相对 2000-01-01) */
    int32_t temp_x1000;   /* 温度 x1000 */
    uint32_t humi_x1000;  /* 湿度 x1000 */
    uint32_t batt_mv;     /* 电池 mV */
    uint16_t sensor1;     /* 模拟量1 原始 */
    uint16_t sensor2;     /* 模拟量2 原始 */
    uint16_t current_mv;  /* 回路采样电压 mV */
    uint8_t valid_bits;   /* 有效位 */
    uint8_t alarm_bits;   /* 当时的报警位 */
} __attribute__((packed)) w25q_log_rec_t;

/* 追加一条日志，返回 true 成功 */
bool w25q_log_append(const w25q_log_rec_t *rec);
/* 读取第 idx 条(0=最新)，返回 true 表示记录有效 */
bool w25q_log_read(uint32_t idx, w25q_log_rec_t *out);
/* 已存日志条数(不超过容量) */
uint32_t w25q_log_count(void);

#endif /* DEV_W25Q_H */
