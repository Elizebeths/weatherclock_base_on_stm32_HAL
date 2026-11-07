/**
 ******************************************************************************
 * @file    gpio_port_stm32.c
 * @brief   L0 引脚抽象 —— STM32F1 实现
 *
 * @note    本文件是 HAL 引脚符号（GPIOA/GPIOB/GPIO_PIN_x/GPIO_MODE_IT_*）在整个
 *          工程里的**唯一出口**：上层只传中性 gpio_pin_t，由这里的一张映射表
 *          翻译成 HAL 类型。
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：旧 lcd.c 的引脚宏（HAL_GPIO_WritePin 封装）抽象化
 *   一级 · 大型开源：ST 官方 STM32CubeF1 —— GPIO_MODE_IT_RISING/FALLING/
 *                    RISING_FALLING 三档 EXTI 模式、HAL_NVIC_SetPriority/
 *                    EnableIRQ 的 IRQn 选取（EXTI0..4 / EXTI9_5 / EXTI15_10）、
 *                    HAL_GPIO_EXTI_Callback 回调式分发
 *   三级 · 小型开源：未使用
 *
 * 中断归属（零改动 CubeMX 生成文件的实现方式）：
 *   CubeMX 生成的 stm32f1xx_it.c 里 EXTIx_IRQHandler() →
 *   HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_x) → HAL_GPIO_EXTI_Callback(GPIO_PIN_x)。
 *   本文件只实现最末端的 Callback，把事件分发给注册表里对应引脚的回调。
 ******************************************************************************
 */

#include "gpio_port.h"

#include "board.h"
#include "main.h" /* HAL —— 仅 L0 实现文件可包含 */

/* ==========================================================================
 * 私有数据：bank → GPIO_TypeDef* 映射表（HAL 引脚符号的唯一出口）
 * ========================================================================== */

static GPIO_TypeDef *const s_bank_map[GPIO_BANK_MAX] = {GPIOA, GPIOB, GPIOC,
                                                        GPIOD};

/** 引脚序号 → 掩码。HAL 的 GPIO_PIN_0..15 恰为 1<<0..1<<15，故可直接移位 */
#define PIN_MASK(pin_) ((uint16_t)(1U << (pin_)))

typedef struct {
  bool used;
  gpio_pin_t pin;
  gpio_irq_cb_t cb;
  void *arg;
} gpio_irq_slot_t;

static gpio_irq_slot_t s_irq_slots[APP_GPIO_IRQ_MAX];

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

static bool pin_valid(gpio_pin_t pin) {
  return (pin.bank < (uint8_t)GPIO_BANK_MAX) && (pin.pin < 16U);
}

/** @brief 引脚序号 → EXTI 中断号（EXTI0..4 各自独立，5..9 与 10..15 分组共享） */
static IRQn_Type irqn_of_pin(uint8_t pin) {
  switch (pin) {
  case 0U:
    return EXTI0_IRQn;
  case 1U:
    return EXTI1_IRQn;
  case 2U:
    return EXTI2_IRQn;
  case 3U:
    return EXTI3_IRQn;
  case 4U:
    return EXTI4_IRQn;
  default:
    return (pin <= 9U) ? EXTI9_5_IRQn : EXTI15_10_IRQn;
  }
}

/** @brief 中性边沿 → HAL EXTI 模式 */
static uint32_t edge_to_mode(gpio_edge_t edge) {
  switch (edge) {
  case GPIO_EDGE_RISING:
    return GPIO_MODE_IT_RISING;
  case GPIO_EDGE_FALLING:
    return GPIO_MODE_IT_FALLING;
  case GPIO_EDGE_BOTH:
  default:
    return GPIO_MODE_IT_RISING_FALLING;
  }
}

static int slot_find_by_pin(gpio_pin_t pin) {
  for (uint32_t i = 0U; i < APP_GPIO_IRQ_MAX; i++) {
    if (s_irq_slots[i].used && s_irq_slots[i].pin.pin == pin.pin &&
        s_irq_slots[i].pin.bank == pin.bank) {
      return (int)i;
    }
  }
  return -1;
}

static int slot_find_free(void) {
  for (uint32_t i = 0U; i < APP_GPIO_IRQ_MAX; i++) {
    if (!s_irq_slots[i].used) {
      return (int)i;
    }
  }
  return -1;
}

/* ==========================================================================
 * 一、电平操作
 * ========================================================================== */

void gpio_port_write(gpio_pin_t pin, bool level) {
  if (!pin_valid(pin)) {
    return;
  }
  HAL_GPIO_WritePin(s_bank_map[pin.bank], PIN_MASK(pin.pin),
                    level ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool gpio_port_read(gpio_pin_t pin) {
  if (!pin_valid(pin)) {
    return false;
  }
  return HAL_GPIO_ReadPin(s_bank_map[pin.bank], PIN_MASK(pin.pin)) == GPIO_PIN_SET;
}

void gpio_port_toggle(gpio_pin_t pin) {
  if (!pin_valid(pin)) {
    return;
  }
  HAL_GPIO_TogglePin(s_bank_map[pin.bank], PIN_MASK(pin.pin));
}

/* ==========================================================================
 * 二、EXTI 中断
 * ========================================================================== */

port_status_t gpio_port_irq_attach(gpio_pin_t pin, gpio_edge_t edge,
                                   gpio_irq_cb_t cb, void *arg) {
  if (!pin_valid(pin) || (cb == NULL)) {
    return PORT_ERROR;
  }

  /* 已注册过同一引脚：仅更新回调与上下文（含边沿重配） */
  const int existing = slot_find_by_pin(pin);
  int slot = existing;

  if (slot < 0) {
    /* F1 上同号引脚共享一条 EXTI 线，禁止两条线落在同一序号 */
    for (uint32_t i = 0U; i < APP_GPIO_IRQ_MAX; i++) {
      if (s_irq_slots[i].used && s_irq_slots[i].pin.pin == pin.pin) {
        return PORT_ERROR;
      }
    }
    slot = slot_find_free();
    if (slot < 0) {
      return PORT_BUSY;
    }
    s_irq_slots[slot].used = true;
    s_irq_slots[slot].pin = pin;
  }

  s_irq_slots[slot].cb = cb;
  s_irq_slots[slot].arg = arg;

  GPIO_InitTypeDef init = {0};
  init.Pin = PIN_MASK(pin.pin);
  init.Mode = edge_to_mode(edge);
  init.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(s_bank_map[pin.bank], &init);

  HAL_NVIC_SetPriority(irqn_of_pin(pin.pin), APP_GPIO_IRQ_PRIORITY, 0U);
  HAL_NVIC_EnableIRQ(irqn_of_pin(pin.pin));

  return PORT_OK;
}

void gpio_port_irq_detach(gpio_pin_t pin) {
  if (!pin_valid(pin)) {
    return;
  }
  const int slot = slot_find_by_pin(pin);
  if (slot < 0) {
    return;
  }

  s_irq_slots[slot].used = false;
  s_irq_slots[slot].cb = NULL;
  s_irq_slots[slot].arg = NULL;

  __HAL_GPIO_EXTI_CLEAR_IT(PIN_MASK(pin.pin));

  /* 该 EXTI 分组若已无注册者，则关闭其中断使能 */
  bool irq_still_used = false;
  for (uint32_t i = 0U; i < APP_GPIO_IRQ_MAX; i++) {
    if (s_irq_slots[i].used &&
        irqn_of_pin(s_irq_slots[i].pin.pin) == irqn_of_pin(pin.pin)) {
      irq_still_used = true;
      break;
    }
  }
  if (!irq_still_used) {
    HAL_NVIC_DisableIRQ(irqn_of_pin(pin.pin));
  }
}

bool gpio_port_irq_is_attached(gpio_pin_t pin) {
  return pin_valid(pin) && (slot_find_by_pin(pin) >= 0);
}

/* ==========================================================================
 * 三、HAL 回调出口
 * ========================================================================== */

/**
 * @brief HAL EXTI 分发入口（由 CubeMX 生成的 EXTIx_IRQHandler 间接调用）
 * @param GPIO_Pin 触发中断的引脚掩码
 * @note  F1 上同号引脚共享 EXTI 线，若同一序号在不同 bank 上都有注册，
 *        回调只能按掩码匹配、无法区分 bank —— 这正是 attach 阶段就禁止
 *        同序号冲突的原因。
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
  for (uint32_t i = 0U; i < APP_GPIO_IRQ_MAX; i++) {
    if (s_irq_slots[i].used && (PIN_MASK(s_irq_slots[i].pin.pin) == GPIO_Pin)) {
      if (s_irq_slots[i].cb != NULL) {
        s_irq_slots[i].cb(s_irq_slots[i].pin, s_irq_slots[i].arg);
      }
      return;
    }
  }
}

/* ==========================================================================
 * 四、软件定时基准
 * ========================================================================== */

uint32_t gpio_port_tick_ms(void) { return HAL_GetTick(); }
