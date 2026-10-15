/**
 * @file    util_ringbuf.h
 * @brief   无锁单生产者/单消费者环形缓冲区（串口 DMA 收发常用）
 *
 * 约束：仅一个生产者 + 一个消费者时可无锁使用（ISR 生产、任务消费即符合）。
 * 容量需为 2 的幂，用掩码取模代替除法，适合中断上下文。
 */
#ifndef UTIL_RINGBUF_H
#define UTIL_RINGBUF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct
{
    uint8_t *buf;
    uint16_t size;          /* 2 的幂 */
    uint16_t mask;          /* size - 1 */
    volatile uint16_t head; /* 写入位置（生产者） */
    volatile uint16_t tail; /* 读取位置（消费者） */
} ringbuf_t;

/* 返回 true 初始化成功；size 非 2 的幂或参数非法返回 false */
bool rb_init(ringbuf_t *rb, uint8_t *backing, uint16_t size);
uint16_t rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len);
uint16_t rb_read(ringbuf_t *rb, uint8_t *out, uint16_t max_len);
uint16_t rb_available(const ringbuf_t *rb); /* 可读字节数 */
uint16_t rb_free(const ringbuf_t *rb);      /* 可写字节数 */
void rb_reset(ringbuf_t *rb);

#endif /* UTIL_RINGBUF_H */
