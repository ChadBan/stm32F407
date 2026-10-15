/**
 * @file    bsp_gpio.c
 * @brief   GPIO/按键 EXTI 实现
 */
#include "bsp_gpio.h"
#include "task.h"

static const key_pin_t g_key_pins[KEY_ID_MAX] = KEY_PIN_TABLE;

/* 每个键一个“曾出现边沿”标志，ISR 置位、任务清除 */
static volatile uint8_t g_key_edge; /* bit i => key_id_t i */

void bsp_gpio_init(void)
{
    /* LED 与按键的 GPIO/EXTI/NVIC 由 CubeMX 完成初始化，
       此处仅复位软件状态。 */
    g_key_edge = 0;
    bsp_led_set(false);
}

void bsp_led_set(bool on)
{
    /* 低电平点亮 */
    HAL_GPIO_WritePin(LED_RUN_PORT, LED_RUN_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void bsp_led_toggle(void)
{
    HAL_GPIO_TogglePin(LED_RUN_PORT, LED_RUN_PIN);
}

bool bsp_key_pressed(key_id_t id)
{
    if (id >= KEY_ID_MAX)
        return false;
    GPIO_PinState s = HAL_GPIO_ReadPin(g_key_pins[id].port, g_key_pins[id].pin);
    return s == GPIO_PIN_SET; /* 下拉输入，按下为高 */
}

bool bsp_key_take_edge(key_id_t id)
{
    if (id >= KEY_ID_MAX)
        return false;
    uint8_t bit = (uint8_t)(1u << id);
    /* 临界区：读-改-写需原子，防止与 ISR 竞争 */
    taskENTER_CRITICAL();
    bool had = (g_key_edge & bit) != 0;
    g_key_edge &= (uint8_t)~bit;
    taskEXIT_CRITICAL();
    return had;
}

/* HAL 强回调：CubeMX 使能 EXTI 后，中断进来在此映射到具体键 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    for (uint8_t i = 0; i < KEY_ID_MAX; i++)
    {
        if (g_key_pins[i].pin == GPIO_Pin)
        {
            g_key_edge |= (uint8_t)(1u << i);
            break;
        }
    }
}
