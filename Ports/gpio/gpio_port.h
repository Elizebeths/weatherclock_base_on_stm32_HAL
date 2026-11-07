/**
 ******************************************************************************
 * @file    gpio_port.h
 * @brief   L0 引脚抽象：写/读/翻转电平、EXTI 中断注册与回调、软件定时基准
 *
 * @note    分层位置：L0（Ports/gpio）。**输入模块与系统基础共用本实现**，
 *          不重复计数、不重复实现。本文件不得出现 HAL / STM32 符号。
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：旧 lcd.c 的 LCD_CS_HIGH()/LCD_CS_LOW()/LCD_DC_HIGH()/
 *                    LCD_DC_LOW()/LCD_BL_on()/LCD_BL_off() —— 全部是
 *                    HAL_GPIO_WritePin(PORT, PIN, SET/RESET) 的宏封装，
 *                    本头文件把「写一个引脚」抽象为 gpio_port_write()，
 *                    引脚由 Config/board.h 的中性 gpio_pin_t 唯一给出
 *   一级 · 大型开源：LVGL 的 indev port（回调式「注册 → 分发」接口风格）
 *                    与 ST 官方 STM32CubeF1 的 EXTI 例程（GPIO_MODE_IT_* +
 *                    HAL_NVIC_EnableIRQ + HAL_GPIO_EXTI_Callback 分发）
 *   三级 · 小型开源：未使用
 *
 * 与旧驱动的关系：旧 lcd.h 依赖 CubeMX 生成的 TFT_xx_Pin 宏定位引脚，
 * 使公开头文件耦合 main.h。本头文件改为「中性 gpio_pin_t（bank + 序号）」，
 * 由 gpio_port_stm32.c 内的一张映射表翻译为 HAL 类型 —— 这是 HAL 引脚符号
 * 在整个工程里的唯一出口。
 *
 * 设计取舍：只暴露上层真实需要的四个操作 + 中断注册，不引入通用
 * 「GPIO 驱动框架」（那是 L1 的 dev_registry 职责），避免 YAGNI 与额外 RAM。
 ******************************************************************************
 */

#ifndef GPIO_PORT_H
#define GPIO_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 以 Ports/ 为根包含同级模块的头文件：整个工程只需 -I Ports 与 -I Config
 * 两个包含路径，避免为每个子目录各登记一条（更少的手工配置 = 更少的失效点）。
 * 依赖方向在这里是可见的：引脚层 → 公共层，单向。 */
#include "common/port.h"

/* ==========================================================================
 * 一、中性引脚描述
 * ========================================================================== */

/** GPIO 分组（对应 STM32 的 GPIOA..GPIOD；LQFP48 上 C/D 仅少数引脚引出） */
typedef enum {
  GPIO_BANK_A = 0,
  GPIO_BANK_B,
  GPIO_BANK_C,
  GPIO_BANK_D,
  GPIO_BANK_MAX
} gpio_bank_t;

/**
 * @brief 一个逻辑引脚：bank + 位序号（0..15）
 * @note  用 Config/board.h 的 BOARD_PIN(bank, pin) 复合字面量构造，
 *        例如 gpio_port_write(BOARD_TFT_CS_PIN, false);
 */
typedef struct {
  uint8_t bank; /**< gpio_bank_t */
  uint8_t pin;  /**< 0..15 */
} gpio_pin_t;

/** EXTI 触发边沿 */
typedef enum {
  GPIO_EDGE_RISING = 0,
  GPIO_EDGE_FALLING,
  GPIO_EDGE_BOTH
} gpio_edge_t;

/**
 * @brief 中断回调
 * @param pin 触发中断的引脚（原样回传给注册方）
 * @param arg 注册时传入的上下文指针
 * @note  在中断上下文中执行，必须短小、不得阻塞、不得调用非 FromISR 的 RTOS API
 */
typedef void (*gpio_irq_cb_t)(gpio_pin_t pin, void *arg);

/* ==========================================================================
 * 二、电平操作
 * ========================================================================== */

/** @brief 写引脚电平（true = 高） */
void gpio_port_write(gpio_pin_t pin, bool level);

/** @brief 读引脚电平（true = 高） */
bool gpio_port_read(gpio_pin_t pin);

/** @brief 翻转引脚电平 */
void gpio_port_toggle(gpio_pin_t pin);

/* ==========================================================================
 * 三、EXTI 中断
 * ========================================================================== */

/**
 * @brief 注册引脚中断（运行时配置边沿并使能 NVIC，回调在中断上下文分发）
 * @param pin  引脚
 * @param edge 触发边沿
 * @param cb   回调；为 NULL 等价于 detach
 * @param arg  回调上下文
 * @retval PORT_OK      注册成功
 * @retval PORT_ERROR   参数非法（pin.bank / pin.pin 越界，或引脚号已被占用）
 * @retval PORT_BUSY    注册表已满（APP_GPIO_IRQ_MAX）
 * @note  STM32F1 上同号引脚共享一条 EXTI 线（PA0/PB0/PC0 共用 EXTI0），
 *        因此已注册的引脚之间必须**引脚序号互不相同**；冲突时返回 PORT_ERROR。
 * @note  前提：CubeMX 中已把该引脚配成 GPIO_EXTI 并在 NVIC 使能，CubeMX 会
 *        生成 EXTIx_IRQHandler（内部调用 HAL_GPIO_EXTI_IRQHandler），
 *        因此本实现无需改动任何生成文件。
 */
port_status_t gpio_port_irq_attach(gpio_pin_t pin, gpio_edge_t edge,
                                   gpio_irq_cb_t cb, void *arg);

/** @brief 注销引脚中断（关闭该 EXTI 线） */
void gpio_port_irq_detach(gpio_pin_t pin);

/** @brief 是否有引脚注册了中断（测试/诊断用） */
bool gpio_port_irq_is_attached(gpio_pin_t pin);

/* ==========================================================================
 * 四、软件定时基准
 * ========================================================================== */

/** @brief 毫秒级软件定时基准（转发 port_time_ms()，便于输入层去抖统一取时） */
uint32_t gpio_port_tick_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_PORT_H */
