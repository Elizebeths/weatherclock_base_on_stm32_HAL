/**
 ******************************************************************************
 * @file    os_port.h
 * @brief   L0 并行原语垫片：任务 / 延时 / 互斥量 / 信号量（中性接口）
 *
 * @note    分层位置：L0（Ports/common）。**本文件不得出现任何 RTOS 或
 *          HAL 符号**——FreeRTOS / CMSIS-RTOS2 的头文件只允许出现在
 *          os_port_stm32.c 里。L1/L3 通过本接口获得并行能力，从而：
 *            · 逻辑层（Devices/、App/）保持零 RTOS 符号，
 *              Ports/check_portability.sh 的 G1-b 判据恒成立。
 *
 * 设计取舍：句柄由**调用方提供存储**（无动态分配），因此不依赖 RTOS 堆，
 *          也不会在运行期失败于内存不足。裸机模式下互斥量退化为
 *          「临界区嵌套计数」、信号量退化为「计数 + 忙等」，
 *          足以支撑 DMA 完成同步，且不引入额外 RAM。
 *
 * 开关：APP_OS_FREERTOS（Config/app_config.h）
 *   0（默认）→ 裸机等效实现；1 → FreeRTOS / CMSIS-RTOS2 实现。
 *   置 1 的前提是已用 CubeMX 为工程加入 FREERTOS 中间件。
 ******************************************************************************
 */

#ifndef OS_PORT_H
#define OS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h" /* 仅取 APP_OS_FREERTOS 等编译期开关 */

/* ==========================================================================
 * 一、对象（调用方持有存储，零动态分配）
 * ========================================================================== */

/** @brief 递归互斥量 */
typedef struct {
  uint32_t depth;  /**< 递归深度 / 裸机嵌套计数 */
  void *handle;    /**< RTOS 句柄（裸机模式恒为 NULL） */
} os_mutex_t;

/** @brief 计数信号量 */
typedef struct {
  volatile uint32_t count; /**< 当前计数 */
  uint32_t max;            /**< 计数上限 */
  void *handle;            /**< RTOS 句柄（裸机模式恒为 NULL） */
} os_sem_t;

/** @brief 任务入口 */
typedef void (*os_task_fn)(void *arg);

/** @brief 任务对象 */
typedef struct {
  os_task_fn fn;        /**< 任务体 */
  void *arg;            /**< 任务参数 */
  const char *name;     /**< 任务名（诊断用） */
  void *handle;         /**< RTOS 句柄（裸机模式恒为 NULL） */
} os_task_t;

/* ==========================================================================
 * 二、生命周期
 * ========================================================================== */

/**
 * @brief 初始化 OS 垫片（幂等）
 * @note  必须在创建任何互斥量/信号量/任务之前调用；由 app_init() 编排。
 *        RTOS 模式下仅记录状态与创建内核对象的公共设施，不启动调度器
 *        （调度器启动由 main.c 在全部初始化完成后负责）。
 */
void os_port_init(void);

/**
 * @brief 当前是否运行在 RTOS 模式
 * @retval true  任务由调度器驱动（刷新由独立任务执行）
 * @retval false 裸机模式（刷新由 app_loop() 轮询驱动）
 */
bool os_port_is_rtos(void);

/* ==========================================================================
 * 三、互斥量（可重入）
 * ========================================================================== */

/** @brief 初始化互斥量（未锁定态） */
void os_port_mutex_init(os_mutex_t *m);

/**
 * @brief 加锁
 * @param timeout_ms 等待上限；0 表示「立即返回」
 * @retval true  已获得锁
 * @retval false 超时（仅 RTOS 模式下可能发生）
 * @note  同一执行流重复加锁是允许的（递归），必须与 unlock 一一配对。
 */
bool os_port_mutex_lock(os_mutex_t *m, uint32_t timeout_ms);

/** @brief 解锁（与 lock 一一配对） */
void os_port_mutex_unlock(os_mutex_t *m);

/* ==========================================================================
 * 四、计数信号量
 * ========================================================================== */

/**
 * @brief 初始化信号量
 * @param initial 初值（上限 0 时按 1 处理）
 * @param max     上限
 */
void os_port_sem_init(os_sem_t *s, uint32_t initial, uint32_t max);

/**
 * @brief 取信号量
 * @param timeout_ms 等待上限；0 表示「立即返回」
 * @retval true  取到
 * @retval false 超时
 */
bool os_port_sem_take(os_sem_t *s, uint32_t timeout_ms);

/** @brief 给信号量（任务上下文） */
void os_port_sem_give(os_sem_t *s);

/**
 * @brief 给信号量（中断上下文）
 * @note  RTOS 模式下使用 FromISR 变体；裸机模式下与 give 等价。
 *        DMA 完成回调运行在中断上下文，必须走本接口。
 */
void os_port_sem_give_isr(os_sem_t *s);

/* ==========================================================================
 * 五、任务与延时
 * ========================================================================== */

/**
 * @brief 创建任务
 * @param t            任务对象（调用方持有存储）
 * @param fn           任务体
 * @param name         任务名
 * @param arg          任务参数
 * @param stack_words  栈深度（**以字为单位**，非字节）
 * @param priority     优先级（数值越大越高）
 * @retval true  已创建（RTOS 模式）或已登记（裸机模式）
 * @retval false 创建失败
 * @note  裸机模式下不创建真实任务：仅登记 fn/arg，由调用方在 app_loop()
 *        中轮询驱动等效入口（见 lcd_refresh_poll 的约定）。
 */
bool os_port_task_create(os_task_t *t, os_task_fn fn, const char *name, void *arg,
                         uint32_t stack_words, uint32_t priority);

/**
 * @brief 毫秒延时
 * @note  RTOS 模式下应让出 CPU（vTaskDelay），裸机模式下等价 port_delay_ms。
 */
void os_port_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* OS_PORT_H */
