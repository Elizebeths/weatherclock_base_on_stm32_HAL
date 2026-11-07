/**
 ******************************************************************************
 * @file    port.h
 * @brief   L0 公共硬件抽象：中性状态码、时基、延时、临界区、时钟与复位
 *
 * @note    分层位置：L0（Ports/common）。本文件**不得**出现任何 HAL / STM32
 *          符号，也不得包含 main.h —— 它是「逻辑层可脱离 HAL 编译」的前提。
 *          唯一允许接触 HAL 的实现文件是 port_stm32.c。
 ******************************************************************************
 *
 * 适配来源
 *   一级 · 大型开源：ST 官方 STM32CubeF1 + CMSIS core_cm3.h
 *                    （DWT->CYCCNT 微秒时基、CoreDebug->DEMCR TRCENA、
 *                      SCB/NVIC_SystemReset、HAL_GetTick 毫秒基准）
 *   一级 · 大型开源：FreeRTOS-Kernel（Cortex-M3 portmacro）的临界区范式
 *                    —— 保存/恢复 PRIMASK + 嵌套计数，PH4 引入 FreeRTOS 后
 *                    可在**不改函数签名**的前提下换成 taskENTER_CRITICAL()
 *   一级 · 大型开源：LVGL port 层「提交/完成」分离与弱耦合接口风格
 *   三级 · 小型开源：未使用
 *
 * 与旧驱动的关系：旧 lcd.c 直接在绘图代码里调用 HAL_Delay()，使延时成为
 * 逻辑层与 HAL 的耦合点（PH1 风险条目明列）。本头文件把延时收敛为
 * port_delay_ms()/port_delay_us()，L1 及以上只能经此调用。
 ******************************************************************************
 */

#ifndef PORT_H
#define PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h" /* 仅取编译期开关（APP_PORT_DWT_ENABLE） */

/* ==========================================================================
 * 一、中性状态码
 * 统一替换散落在旧代码里的 bool / HAL_StatusTypeDef，使跨平台语义一致。
 * ========================================================================== */

typedef enum {
  PORT_OK = 0,  /**< 成功 */
  PORT_TIMEOUT, /**< 等待超时（总线无响应 / 设备未就绪） */
  PORT_ERROR,   /**< 硬件错误（总线错误 / 参数非法 / 中断传输失败） */
  PORT_BUSY     /**< 资源忙（上一次传输尚未完成） */
} port_status_t;

/** @brief 状态码转可读字符串（日志用，返回静态常量字符串） */
const char *port_status_str(port_status_t status);

/* ==========================================================================
 * 二、生命周期
 * ========================================================================== */

/**
 * @brief 初始化 Port 公共设施（DWT CYCCNT 微秒时基）。
 * @note  必须在 HAL_Init() 与 SystemClock_Config() 之后调用一次；
 *        幂等，重复调用无副作用。
 */
void port_init(void);

/** @brief 是否已初始化；未初始化时 port_delay_us() 会退化为忙等循环 */
bool port_is_initialized(void);

/* ==========================================================================
 * 三、时基与周期计数
 * ========================================================================== */

/**
 * @brief 取 DWT CYCCNT 原始周期计数。
 * @note  这是 PH1 性能测量（S0 基线 / S3 复测）的唯一计时来源，
 *        与逻辑分析仪交叉验证。未初始化或关闭埋点时返回 0。
 */
uint32_t port_cycles(void);

/** @brief 每微秒的 CPU 周期数（72 MHz → 72），用于把周期差换算为时间 */
uint32_t port_cycles_per_us(void);

/** @brief 微秒级单调时基（由 CYCCNT 换算，约 59.6 s 回绕） */
uint32_t port_time_us(void);

/** @brief 毫秒级单调时基（HAL SysTick，约 49.7 天回绕） */
uint32_t port_time_ms(void);

/* ==========================================================================
 * 四、延时
 * ========================================================================== */

/**
 * @brief 毫秒级延时（会让出 CPU，进入忙等/SysTick 循环）
 * @note  替代旧代码里的 HAL_Delay()。禁止在 L1 及以上直接调用 HAL_Delay。
 */
void port_delay_ms(uint32_t ms);

/**
 * @brief 微秒级忙等延时（DWT 驱动，精度约 ±1 µs）
 * @param  us 延时微秒数；为 0 时立即返回
 * @note  依赖 port_init()；未初始化时退化为按 CPU 频率估算的忙等。
 */
void port_delay_us(uint32_t us);

/* ==========================================================================
 * 五、临界区（可嵌套）
 * ========================================================================== */

/**
 * @brief 进入临界区（可嵌套）。
 * @note  实现为「嵌套计数 + PRIMASK 保存/恢复」：只有最外层会真正关中断，
 *        因此可以安全地在已关中断的上下文中再调用。
 */
void port_enter_critical(void);

/** @brief 退出临界区（与 port_enter_critical() 一一配对） */
void port_exit_critical(void);

/** @brief 当前临界区嵌套深度（0 表示不在临界区） */
uint32_t port_critical_nesting(void);

/* ==========================================================================
 * 六、时钟与复位
 * ========================================================================== */

/** @brief 当前 CPU（HCLK）频率，单位 Hz（72 MHz → 72000000） */
uint32_t port_clk_hz(void);

/** @brief 软复位 MCU（NVIC_SystemReset），用于 watchdog 触发的受控重启 */
void port_system_reset(void);

/* ==========================================================================
 * 七、断言支持（供 L1 / L2 的防御性检查使用）
 * ========================================================================== */

/** @brief 断言失败钩子：关中断并原地停留，等待调试器接入 */
void port_assert_failed(const char *file, int line);

/** 编译期断言（C99）：条件不成立时产生负长度的数组，编译即报错 */
#define APP_STATIC_ASSERT(cond_, tag_)                                         \
  typedef char app_static_assert_##tag_[(cond_) ? 1 : -1]

/** 运行期断言：失败即调用 port_assert_failed() */
#define APP_ASSERT(cond_)                                                      \
  do {                                                                         \
    if (!(cond_)) {                                                            \
      port_assert_failed(__FILE__, __LINE__);                                  \
    }                                                                          \
  } while (0)

#ifdef __cplusplus
}
#endif

#endif /* PORT_H */
