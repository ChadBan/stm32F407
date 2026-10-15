/**
 * @file    app_tasks.h
 * @brief   FreeRTOS 任务入口声明
 */
#ifndef APP_TASKS_H
#define APP_TASKS_H

#include <stdbool.h>
#include "FreeRTOS.h"

void collect_task(void *arg);
void display_task(void *arg);
void key_task(void *arg);
void comm_task(void *arg);
void alarm_task(void *arg);

/* 创建并启动所有业务任务（在 main 中 app_state_init 之后调用） */
bool app_tasks_start(void);

#endif /* APP_TASKS_H */