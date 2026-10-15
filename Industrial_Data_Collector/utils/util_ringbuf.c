/**
 * @file    util_ringbuf.c
 * @brief   环形缓冲区实现（详见 util_ringbuf.h）
 */
#include "util_ringbuf.h"

/* 判断是否为 2 的幂（且非 0） */
static inline bool is_pow2(uint16_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

bool rb_init(ringbuf_t *rb, uint8_t *backing, uint16_t size)
{
    if (rb == NULL || backing == NULL || !is_pow2(size))
    {
        return false;
    }
    rb->buf = backing;
    rb->size = size;
    rb->mask = size - 1;
    rb->head = 0;
    rb->tail = 0;
    return true;
}

uint16_t rb_available(const ringbuf_t *rb)
{
    return (uint16_t)((rb->head - rb->tail) & rb->mask);
}

uint16_t rb_free(const ringbuf_t *rb)
{
    /* 预留一个字节区分“满”与“空” */
    return (uint16_t)(rb->size - 1 - rb_available(rb));
}

uint16_t rb_write(ringbuf_t *rb, const uint8_t *data, uint16_t len)
{
    if (rb == NULL || data == NULL)
    {
        return 0;
    }
    uint16_t written = 0;
    uint16_t head = rb->head;
    while (written < len)
    {
        uint16_t next = (uint16_t)((head + 1) & rb->mask);
        if (next == rb->tail)
        {
            break; /* 满，停止写入 */
        }
        rb->buf[head] = data[written++];
        head = next;
    }
    rb->head = head; /* 数据写完再更新 head，保证消费者看到一致状态 */
    return written;
}

uint16_t rb_read(ringbuf_t *rb, uint8_t *out, uint16_t max_len)
{
    if (rb == NULL || out == NULL)
    {
        return 0;
    }
    uint16_t rd = 0;
    uint16_t tail = rb->tail;
    while (rd < max_len && tail != rb->head)
    {
        out[rd++] = rb->buf[tail];
        tail = (uint16_t)((tail + 1) & rb->mask);
    }
    rb->tail = tail;
    return rd;
}

void rb_reset(ringbuf_t *rb)
{
    if (rb != NULL)
    {
        rb->head = 0;
        rb->tail = 0;
    }
}
