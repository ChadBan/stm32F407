/**
 * @file    app_state.c
 * @brief   共享状态中心实现
 */
#include "app_state.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "dev_w25q.h"
#include <string.h>

static QueueHandle_t g_q_data;
static SemaphoreHandle_t g_lock; /* 保护 params / sample / alarm / page */

static device_params_t g_params;
static collect_sample_t g_last;
static bool g_last_valid;
static uint8_t g_param_field = 0;
static volatile uint32_t g_last_activity_ms;
static uint8_t g_alarm_bits;
static disp_page_t g_page = PAGE_MAIN;

/* 上电默认参数 */
static const device_params_t PARAM_DEFAULT = {
    .modbus_addr = 1,
    .temp_alarm_hi = 600,  /* 60.0 C x10 */
    .temp_alarm_lo = -100, /* -10.0 C x10 */
    .batt_alarm_mv = 3300,
    .baud_code = 0, /* 9600 */
    .mute = 0,
    .sleep_en = 0, /* 默认关闭 STOP 休眠，现场按需开启 */
};

bool app_state_init(void)
{
    g_lock = xSemaphoreCreateMutex();
    g_q_data = xQueueCreate(QUEUE_DEPTH_DATA, sizeof(collect_sample_t));
    if (g_lock == NULL || g_q_data == NULL)
        return false;

    /* 从 Flash 恢复参数；无有效数据(首刷)则用默认值 */
    g_params = PARAM_DEFAULT;
    uint16_t n = w25q_load_params((uint8_t *)&g_params, sizeof(g_params));
    if (n != sizeof(g_params))
    {
        g_params = PARAM_DEFAULT;
        w25q_save_params((const uint8_t *)&g_params, sizeof(g_params));
    }
    /* 载入后做基本合法性校验，防止 Flash 脏数据导致越界 */
    if (g_params.modbus_addr == 0 || g_params.modbus_addr > 247)
        g_params.modbus_addr = PARAM_DEFAULT.modbus_addr;
    if (g_params.baud_code > 4)
        g_params.baud_code = 0;
    g_last_activity_ms = HAL_GetTick();
    return true;
}

QueueHandle_t app_data_queue(void) { return g_q_data; }

static void lock(void)
{
    if (g_lock)
        xSemaphoreTake(g_lock, portMAX_DELAY);
}
static void unlock(void)
{
    if (g_lock)
        xSemaphoreGive(g_lock);
}

void app_get_params(device_params_t *out)
{
    if (out == NULL)
        return;
    lock();
    *out = g_params;
    unlock();
}

void app_set_params(const device_params_t *in)
{
    if (in == NULL)
        return;
    lock();
    g_params = *in;
    unlock();
    /* 落盘放在锁外，避免长时间 SPI 擦写占用互斥锁阻塞显示 */
    w25q_save_params((const uint8_t *)in, sizeof(*in));
}

void app_update_sample(const collect_sample_t *s)
{
    if (s == NULL)
        return;
    lock();
    g_last = *s;
    g_last_valid = true;
    unlock();
}

bool app_get_last_sample(collect_sample_t *out)
{
    if (out == NULL)
        return false;
    lock();
    bool v = g_last_valid;
    if (v)
        *out = g_last;
    unlock();
    return v;
}

uint8_t app_get_alarm_bits(void)
{
    lock();
    uint8_t b = g_alarm_bits;
    unlock();
    return b;
}

void app_set_alarm_bits(uint8_t bits)
{
    lock();
    g_alarm_bits = bits;
    unlock();
}

void app_toggle_alarm_bit(uint8_t bit, bool on)
{
    lock();
    if (on)
        g_alarm_bits |= bit;
    else
        g_alarm_bits &= (uint8_t)~bit;
    unlock();
}

disp_page_t app_get_page(void)
{
    lock();
    disp_page_t p = g_page;
    unlock();
    return p;
}

void app_set_page(disp_page_t p)
{
    if (p >= PAGE_COUNT)
        p = PAGE_MAIN;
    lock();
    g_page = p;
    unlock();
}

uint8_t app_get_param_field(void)
{
    lock();
    uint8_t f = g_param_field;
    unlock();
    return f;
}

void app_set_param_field(uint8_t field)
{
    lock();
    g_param_field = field;
    unlock();
}

void app_mark_activity(void)
{
    g_last_activity_ms = HAL_GetTick();
}

uint32_t app_ms_since_activity(void)
{
    return (uint32_t)(HAL_GetTick() - g_last_activity_ms);
}
