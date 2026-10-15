/**
 * @file    app_state.h
 * @brief   全局共享状态中心：参数、最新采样、报警位、数据队列
 *
 * 提供线程安全的读写访问。params 读被 modbus/显示用，写被 modbus/按键用，
 * 所有对 params 的访问经互斥锁保护；写后调用 app_state_persist_params() 落盘。
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include "app_types.h"
#include "queue.h"

/* 初始化状态中心：创建队列/互斥锁，从 Flash 载入参数 */
bool app_state_init(void);

/* 采集数据队列（collect -> display/comm） */
QueueHandle_t app_data_queue(void);

/* 参数：拷贝式读写（内部加锁） */
void app_get_params(device_params_t *out);
void app_set_params(const device_params_t *in); /* 内存更新 + 落 Flash */

/* 最新采样快照（加锁拷贝），返回是否有有效数据 */
bool app_get_last_sample(collect_sample_t *out);
void app_update_sample(const collect_sample_t *s);

/* 报警位图（原子读改写） */
uint8_t app_get_alarm_bits(void);
void app_set_alarm_bits(uint8_t bits);
void app_toggle_alarm_bit(uint8_t bit, bool on);

/* 显示页面切换（按键任务写，显示任务读） */
disp_page_t app_get_page(void);
void app_set_page(disp_page_t p);

/* 参数编辑光标（key 任务写，display 任务读） */
uint8_t app_get_param_field(void);
void app_set_param_field(uint8_t field);

/* 低功耗：最近活动时间戳与休眠计时（主循环/空闲钩子用） */
void app_mark_activity(void);
uint32_t app_ms_since_activity(void);

#endif /* APP_STATE_H */
