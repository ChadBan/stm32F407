###############################################################################
# Makefile - Industrial_Data_Collector (STM32F407ZGT6)
# 工具链: arm-none-eabi-gcc (macOS: brew install --cask gcc-arm-embedded)
# 用法:   make          编译出 build/*.elf|hex|bin
#         make clean    清理
#         make flash    通过 openocd + ST-Link 烧录(需自行配置 board 脚本)
###############################################################################

TARGET   := industrial_collector
BUILD    := build

CROSS    ?= /Applications/ArmGNUToolchain/15.3.rel2/arm-none-eabi/bin/arm-none-eabi-
CC        = $(CROSS)gcc
AS        = $(CROSS)gcc
LD        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy
OBJDUMP   = $(CROSS)objdump
SIZE      = $(CROSS)size
MKDIR     = mkdir -p

# ---- CPU / FPU ----
CPU      := -mcpu=cortex-m4
FPU      := -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS     := -DUSE_HAL_DRIVER -DSTM32F407xx

# ---- include 路径 ----
INC := \
  -IInc \
  -IDrivers/CMSIS/Include \
  -IDrivers/CMSIS/Device/ST/STM32F4xx/Include \
  -IDrivers/STM32F4xx_HAL_Driver/Inc \
  -IMiddlewares/Third_Party/FreeRTOS/Source/include \
  -IMiddlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F \
  -Iutils \
  -Ibsp \
  -Idevice \
  -Iapp

# ---- 编译选项 ----
CFLAGS   := $(CPU) $(FPU) -mthumb $(DEFS) $(INC) \
            -Wall -Wextra -Wshadow -Wundef \
            -ffunction-sections -fdata-sections \
            -fno-common -fno-builtin \
            -O2 -g3 -std=gnu11

ASFLAGS  := $(CPU) $(FPU) -mthumb -g3 -x assembler-with-cpp $(DEFS) $(INC)

LDFLAGS  := $(CPU) $(FPU) -mthumb -T STM32F407ZGTx_FLASH.ld \
            -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
            --specs=nosys.specs -u _printf_float

# ---- HAL 源文件（按 stm32f4xx_hal_conf.h 开启的模块裁剪） ----
HAL_DIR := Drivers/STM32F4xx_HAL_Driver/Src
HAL_SRCS := \
  stm32f4xx_hal.c \
  stm32f4xx_hal_adc.c \
  stm32f4xx_hal_adc_ex.c \
  stm32f4xx_hal_cortex.c \
  stm32f4xx_hal_dma.c \
  stm32f4xx_hal_dma_ex.c \
  stm32f4xx_hal_flash.c \
  stm32f4xx_hal_flash_ex.c \
  stm32f4xx_hal_gpio.c \
  stm32f4xx_hal_i2c.c \
  stm32f4xx_hal_i2c_ex.c \
  stm32f4xx_hal_iwdg.c \
  stm32f4xx_hal_pwr.c \
  stm32f4xx_hal_pwr_ex.c \
  stm32f4xx_hal_rcc.c \
  stm32f4xx_hal_rcc_ex.c \
  stm32f4xx_hal_rtc.c \
  stm32f4xx_hal_rtc_ex.c \
  stm32f4xx_hal_spi.c \
  stm32f4xx_hal_tim.c \
  stm32f4xx_hal_tim_ex.c \
  stm32f4xx_hal_uart.c \
  stm32f4xx_hal_wwdg.c

# ---- FreeRTOS 源文件 ----
RTOS_DIR := Middlewares/Third_Party/FreeRTOS/Source
RTOS_SRCS := \
  tasks.c \
  queue.c \
  list.c \
  timers.c \
  event_groups.c \
  stream_buffer.c \
  portable/GCC/ARM_CM4F/port.c \
  portable/MemMang/heap_4.c

# ---- CMSIS 器件支持 ----
CMSIS_SRCS := \
  Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c

# ---- 应用层源文件 ----
APP_SRCS := \
  $(wildcard Src/*.c) \
  $(wildcard utils/*.c) \
  $(wildcard bsp/*.c) \
  $(wildcard device/*.c) \
  $(wildcard app/*.c)

SRCS := $(addprefix $(HAL_DIR)/,$(HAL_SRCS)) \
        $(addprefix $(RTOS_DIR)/,$(RTOS_SRCS)) \
        $(CMSIS_SRCS) \
        $(APP_SRCS)

# 启动文件(gcc 版)
STARTUP_SRC := Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/startup_stm32f407xx.s

OBJS := $(SRCS:%.c=$(BUILD)/%.o) $(STARTUP_SRC:%.s=$(BUILD)/%.o)

.PHONY: all clean flash

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).hex $(BUILD)/$(TARGET).bin
	@$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/%.o: %.c
	@$(MKDIR) $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: %.s
	@$(MKDIR) $(dir $@)
	$(AS) $(ASFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(BUILD)/$(TARGET).hex: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: $(BUILD)/$(TARGET).elf
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
	        -c "program $(BUILD)/$(TARGET).elf verify reset exit"

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
