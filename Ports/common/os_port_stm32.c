/**
 ******************************************************************************
 * @file    os_port_stm32.c
 * @brief   L0 并行原语垫片 —— 目标实现（裸机等效 / FreeRTOS 两档后端）
 *
 * @note    本文件是本工程**唯一**允许出现 RTOS 头文件的位置。
 *          APP_OS_FREERTOS == 0（默认）：裸机等效实现 —— 互斥量退化为
 *          临界区嵌套计数、信号量退化为「计数 + 忙等」，任务只登记不创建；
 *          APP_OS_FREERTOS == 1：使用 CubeMX 生成的 FreeRTOS（CMSIS-RTOS2）
 *          内核对象。两种后端对外接口完全一致，L1/L3 一行不改。
 *
 ******************************************************************************
 */

#include "os_port.h"

#include <stddef.h>

#include "app_config.h"
#include "common/port.h"

#if APP_OS_FREERTOS
#include "cmsis_os2.h" /* 仅在启用 FreeRTOS 时包含 */
#endif

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

void os_port_init(void) {
#if APP_OS_FREERTOS
  /* 内核对象均在 os_port_*_init 中按需创建；调度器由 main.c 启动 */
#endif
}

bool os_port_is_rtos(void) {
#if APP_OS_FREERTOS
  return true;
#else
  return false;
#endif
}

/* ==========================================================================
 * 互斥量
 * ========================================================================== */

void os_port_mutex_init(os_mutex_t *m) {
  if (m == NULL) {
    return;
  }
  m->depth = 0U;
  m->handle = NULL;
#if APP_OS_FREERTOS
  m->handle = (void *)osMutexNew(NULL);
#endif
}

bool os_port_mutex_lock(os_mutex_t *m, uint32_t timeout_ms) {
  if (m == NULL) {
    return false;
  }
#if APP_OS_FREERTOS
  const uint32_t ticks = (timeout_ms == 0U) ? 0U : osKernelGetTickFreq() * timeout_ms / 1000U;
  return osMutexAcquire((osMutexId_t)m->handle,
                        (timeout_ms == UINT32_MAX) ? osWaitForever : ticks) == osOK;
#else
  (void)timeout_ms;
  /* 裸机：单执行流 + 中断，互斥量只需可重入计数；关中断保护计数更新，
   * 不会长时间关中断（与 spi_port 的总线锁同一范式）。 */
  port_enter_critical();
  m->depth++;
  port_exit_critical();
  return true;
#endif
}

void os_port_mutex_unlock(os_mutex_t *m) {
  if (m == NULL) {
    return;
  }
#if APP_OS_FREERTOS
  (void)osMutexRelease((osMutexId_t)m->handle);
#else
  port_enter_critical();
  if (m->depth > 0U) {
    m->depth--;
  }
  port_exit_critical();
#endif
}

/* ==========================================================================
 * 信号量
 * ========================================================================== */

void os_port_sem_init(os_sem_t *s, uint32_t initial, uint32_t max) {
  if (s == NULL) {
    return;
  }
  s->max = (max == 0U) ? 1U : max;
  s->count = (initial > s->max) ? s->max : initial;
  s->handle = NULL;
#if APP_OS_FREERTOS
  s->handle = (void *)osSemaphoreNew(s->max, s->count, NULL);
#endif
}

bool os_port_sem_take(os_sem_t *s, uint32_t timeout_ms) {
  if (s == NULL) {
    return false;
  }
#if APP_OS_FREERTOS
  const uint32_t ticks = (timeout_ms == 0U) ? 0U : osKernelGetTickFreq() * timeout_ms / 1000U;
  return osSemaphoreAcquire((osSemaphoreId_t)s->handle,
                            (timeout_ms == UINT32_MAX) ? osWaitForever : ticks) == osOK;
#else
  /* 裸机：忙等 + 超时。用于等待 DMA 完成（中断会让出计数值）。 */
  const uint32_t start = port_time_ms();
  for (;;) {
    bool got = false;
    port_enter_critical();
    if (s->count > 0U) {
      s->count--;
      got = true;
    }
    port_exit_critical();
    if (got) {
      return true;
    }
    if ((timeout_ms == 0U) ||
        (uint32_t)(port_time_ms() - start) >= timeout_ms) {
      return false;
    }
    port_delay_us(50U); /* 让步节拍：避免在中断密集时长时间忙等 */
  }
#endif
}

void os_port_sem_give(os_sem_t *s) {
  if (s == NULL) {
    return;
  }
#if APP_OS_FREERTOS
  (void)osSemaphoreRelease((osSemaphoreId_t)s->handle);
#else
  port_enter_critical();
  if (s->count < s->max) {
    s->count++;
  }
  port_exit_critical();
#endif
}

void os_port_sem_give_isr(os_sem_t *s) {
  if (s == NULL) {
    return;
  }
  /* 裸机实现本身是临界区安全的，中断上下文可直接复用 give 路径 */
  os_port_sem_give(s);
}

/* ==========================================================================
 * 任务与延时
 * ========================================================================== */

bool os_port_task_create(os_task_t *t, os_task_fn fn, const char *name, void *arg,
                         uint32_t stack_words, uint32_t priority) {
  if ((t == NULL) || (fn == NULL)) {
    return false;
  }
  t->fn = fn;
  t->arg = arg;
  t->name = name;
  t->handle = NULL;
#if APP_OS_FREERTOS
  osThreadAttr_t attr = {0};
  attr.name = name;
  attr.stack_size = stack_words * sizeof(uint32_t);
  attr.priority = (osPriority_t)priority;
  t->handle = (void *)osThreadNew((osThreadFunc_t)fn, arg, &attr);
  return t->handle != NULL;
#else
  (void)stack_words;
  (void)priority;
  /* 裸机：不创建真实任务。调用方按 os_port_is_rtos() 决定是否用
   * 轮询入口（如 lcd_refresh_poll）驱动等效任务体。 */
  return true;
#endif
}

void os_port_delay_ms(uint32_t ms) {
  if (ms == 0U) {
    return;
  }
#if APP_OS_FREERTOS
  osDelay(ms);
#else
  port_delay_ms(ms);
#endif
}
