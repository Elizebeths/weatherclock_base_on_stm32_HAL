/**
 ******************************************************************************
 * @file    port_stm32.c
 * @brief   L0 公共硬件抽象 —— STM32F1 实现（本工程唯一接触 HAL/CMSIS 的实现之一）
 *
 * @note    本文件实现 port.h 的全部符号。DWT CYCCNT 提供微秒时基与性能埋点，
 *          HAL SysTick 提供毫秒基准，PRIMASK 提供可嵌套临界区。
 ******************************************************************************
 *
 * 适配来源
 *   一级 · 大型开源：ST 官方 STM32CubeF1 与 CMSIS core_cm3.h
 *     - DWT 使能序列（CoreDebug->DEMCR TRCENA → DWT->CTRL CYCCNTENA →
 *       DWT->CYCCNT 清零）沿用 ST/ARM 官方推荐写法
 *     - 毫秒基准直接用 HAL_GetTick()（SysTick），避免另起定时器资源
 *     - 软复位用 CMSIS NVIC_SystemReset()
 *   一级 · 大型开源：FreeRTOS-Kernel（Cortex-M3 portmacro）临界区范式
 *     - 「保存 PRIMASK → 关中断 → 嵌套计数 → 最外层恢复」的结构与
 *       portSET_INTERRUPT_MASK_FROM_ISR()/portCLEAR_INTERRUPT_MASK_FROM_ISR()
 *       同构，便于 PH4 引入 FreeRTOS 后平滑替换
 *   二级 · 自有模块：旧 lcd.c 中散落的 HAL_Delay(20)/HAL_Delay(120) 被替换为
 *                    port_delay_ms()，消除逻辑层的 HAL 耦合
 *   三级 · 小型开源：未使用
 ******************************************************************************
 */

#include "port.h"

#include "main.h" /* HAL + CMSIS（core_cm3.h）—— 仅 L0 实现文件可包含 */

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

static bool s_initialized = false;
static uint32_t s_cycles_per_us = 0U;

/** 临界区嵌套深度与进入前保存的 PRIMASK */
static uint32_t s_critical_nesting = 0U;
static uint32_t s_critical_primask = 0U;

/* ==========================================================================
 * 一、生命周期
 * ========================================================================== */

void port_init(void) {
  if (s_initialized) {
    return;
  }

  /* 按 HCLK 计算每微秒的周期数；HAL_RCC_GetHCLKFreq() 由 CubeMX 的时钟树给出 */
  const uint32_t hclk = HAL_RCC_GetHCLKFreq();
  s_cycles_per_us = (hclk + 500000U) / 1000000U; /* 四舍五入；72 MHz → 72 */
  if (s_cycles_per_us == 0U) {
    s_cycles_per_us = 1U; /* 防御：至少为 1，避免除零与死循环 */
  }

#if APP_PORT_DWT_ENABLE
  /* CMSIS 官方 DWT CYCCNT 使能序列 */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#endif

  s_initialized = true;
}

bool port_is_initialized(void) { return s_initialized; }

/* ==========================================================================
 * 二、时基与周期计数
 * ========================================================================== */

uint32_t port_cycles(void) {
#if APP_PORT_DWT_ENABLE
  return s_initialized ? DWT->CYCCNT : 0U;
#else
  return 0U;
#endif
}

uint32_t port_cycles_per_us(void) { return s_cycles_per_us; }

uint32_t port_time_us(void) {
  if (s_cycles_per_us == 0U) {
    return 0U;
  }
#if APP_PORT_DWT_ENABLE
  /* CYCCNT 约 59.6 s 回绕（72 MHz 下 2^32/72e6）；换算后同样回绕，语义一致 */
  return DWT->CYCCNT / s_cycles_per_us;
#else
  /* 关闭 DWT 时退化为毫秒基准换算，精度降至 1 ms */
  return HAL_GetTick() * 1000U;
#endif
}

uint32_t port_time_ms(void) { return HAL_GetTick(); }

/* ==========================================================================
 * 三、延时
 * ========================================================================== */

void port_delay_ms(uint32_t ms) {
  if (ms == 0U) {
    return;
  }
  HAL_Delay(ms);
}

void port_delay_us(uint32_t us) {
  if (us == 0U) {
    return;
  }

#if APP_PORT_DWT_ENABLE
  if (s_initialized && s_cycles_per_us != 0U) {
    /* 单次最长约 59.6 s（72 MHz 下 us * 72 不溢出的上限）；超长请改用 ms 接口 */
    const uint32_t wait = us * s_cycles_per_us;
    const uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < wait) {
      /* 无符号差值比较天然处理 CYCCNT 回绕 */
    }
    return;
  }
#endif

  /* 未初始化时的退化路径：按 CPU 频率粗略估算一轮循环约 4 个周期 */
  const uint32_t loops = (us * (s_cycles_per_us ? s_cycles_per_us : 72U)) / 4U;
  for (volatile uint32_t i = 0U; i < loops; i++) {
    __NOP();
  }
}

/* ==========================================================================
 * 四、临界区（可嵌套）
 * ========================================================================== */

void port_enter_critical(void) {
  if (s_critical_nesting == 0U) {
    /* 只在最外层保存并关中断，保证嵌套安全 */
    s_critical_primask = __get_PRIMASK();
    __disable_irq();
  }
  s_critical_nesting++;
}

void port_exit_critical(void) {
  if (s_critical_nesting == 0U) {
    return; /* 防御：未配对调用不应打开中断 */
  }
  s_critical_nesting--;
  if (s_critical_nesting == 0U) {
    if (s_critical_primask == 0U) {
      __enable_irq(); /* 恢复进入前的状态，而非无条件开中断 */
    }
  }
}

uint32_t port_critical_nesting(void) { return s_critical_nesting; }

/* ==========================================================================
 * 五、时钟与复位
 * ========================================================================== */

uint32_t port_clk_hz(void) { return HAL_RCC_GetHCLKFreq(); }

void port_system_reset(void) { NVIC_SystemReset(); }

/* ==========================================================================
 * 六、断言与状态码
 * ========================================================================== */

void port_assert_failed(const char *file, int line) {
  (void)file;
  (void)line;
  __disable_irq();
  for (;;) {
    /* 停留在此处，等待调试器检查调用栈与被断言的表达式 */
  }
}

const char *port_status_str(port_status_t status) {
  switch (status) {
  case PORT_OK:
    return "OK";
  case PORT_TIMEOUT:
    return "TIMEOUT";
  case PORT_ERROR:
    return "ERROR";
  case PORT_BUSY:
    return "BUSY";
  default:
    return "UNKNOWN";
  }
}
