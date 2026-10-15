/**
 * @file    app_tasks.c
 * @brief   业务任务统一创建入口
 */
#include "app_tasks.h"
#include "board_config.h"
#include "FreeRTOS.h"
#include "task.h"

static bool create_task(TaskFunction_t fn, const char *name, uint16_t stack, UBaseType_t prio)
{
    return xTaskCreate(fn, name, stack, NULL, prio, NULL) == pdPASS;
}

bool app_tasks_start(void)
{
    bool ok = true;
    ok &= create_task(collect_task, "collect", TASK_STACK_COLLECT, TASK_PRIO_COLLECT);
    ok &= create_task(comm_task, "comm", TASK_STACK_COMM, TASK_PRIO_COMM);
    ok &= create_task(key_task, "key", TASK_STACK_KEY, TASK_PRIO_KEY);
    ok &= create_task(alarm_task, "alarm", TASK_STACK_ALARM, TASK_PRIO_ALARM);
    ok &= create_task(display_task, "display", TASK_STACK_DISPLAY, TASK_PRIO_DISPLAY);
    return ok;
}
