/**
 ******************************************************************************
 * @file    elog_port.c
 * @brief   EasyLogger 移植层 —— 复用本工程 L0 预留的日志通道与临界区
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：本工程 Ports/（L0）与 Config/ 已为日志预留接口；
 *                    本文件是其上唯一需要新增的 EasyLogger 桥接代码
 *   一级 · 大型开源：armink/EasyLogger 官方 port 模板
 *                    （easylogger/port/elog_port.c）与
 *                    demo/non_os/stm32f10x 的 stm32 移植样例
 *
 * 与上游 demo 移植的差异（「以本工程的结构和配置为准」）：
 *   1) 输出：由 printf/fputc 改为 uart_port_write(UART_PORT_CH_LOG, …)，
 *      即 Ports/bus 预留给日志的 **USART1** 通道（详见 Ports/bus/uart_port.h），
 *      因此本文件**不出现任何 HAL 符号**，日志链路与 uart_port 解耦；
 *      同时避免侵入 CubeMX 生成的 syscalls.c / 重定向 _write。
 *   2) 互斥：由裸 __disable_irq() 改为 Ports/common 的 port_enter_critical()/
 *      port_exit_critical()（可嵌套），与 uart_port 自身的通道锁同源，避免
 *      「日志输出锁」与「总线锁」嵌套时误开中断。
 *   3) 时间：由固定字符串改为工程毫秒时基 port_time_ms()，无需 RTC。
 *   4) 提供上游模板要求但 demo 缺失的 elog_port_deinit()，避免链接期缺符号。
 *
 * 前置条件
 *   - Config/board.h 中 BOARD_UART_LOG_ENABLE == 1，且 CubeMX 已启用 USART1
 *     （PA9=TX / PA10=RX，115200 8N1）；未启用时日志通道被判为不可用，
 *     uart_port_write() 返回错误且不输出，但不会影响其它功能。
 *   - 建议在 MX_USART1_UART_Init() 之后调用 uart_port_init()，再 elog_init()。
 ******************************************************************************
 */

#include "elog.h"

#include "app_config.h"
#include "bus/uart_port.h" /* L0 日志通道（USART1）；不含 HAL 符号 */
#include "common/port.h"   /* 临界区与毫秒时基 */

#include <stddef.h>
#include <stdio.h>

/* elog_port_get_time() 须返回以 '\0' 结尾的字符串指针，这里用静态缓冲
 * 按「秒.毫秒」格式化开机时间。单次输出期间由 elog 的输出锁保护。 */
static char s_time_str[16];

/**
 * @brief EasyLogger 移植层初始化
 * @note  日志通道（USART1）的生命周期由 uart_port 统一管理，此处无额外初始化。
 */
ElogErrCode elog_port_init(void) {
  return ELOG_NO_ERR;
}

/**
 * @brief EasyLogger 移植层反初始化
 * @note  无需要释放的资源；保留以匹配 elog.c 的调用契约。
 */
void elog_port_deinit(void) {
  /* 无资源需要释放 */
}

/**
 * @brief 日志输出接口：写入日志通道（USART1，轮询发送、带超时）
 * @param log  已格式化的日志文本（未必以 '\0' 结尾）
 * @param size 文本长度（字节）
 */
void elog_port_output(const char *log, size_t size) {
  if ((log == NULL) || (size == 0U) || (size > 0xFFFFU)) {
    return;
  }
  /* 复用预留接口：故障（如当前发送忙）时返回错误，仅丢弃本行，不阻塞调用者 */
  (void)uart_port_write(UART_PORT_CH_LOG, (const uint8_t *)log,
                        (uint16_t)size, APP_UART_TX_TIMEOUT_MS);
}

/**
 * @brief 输出加锁（可嵌套）
 * @note  与 uart_port 的通道锁共用 port 临界区，嵌套安全。
 */
void elog_port_output_lock(void) {
  port_enter_critical();
}

/**
 * @brief 输出解锁
 */
void elog_port_output_unlock(void) {
  port_exit_critical();
}

/**
 * @brief 当前时间接口
 * @return 形如 "12.345"（开机以来的 秒.毫秒）的静态字符串
 */
const char *elog_port_get_time(void) {
  const uint32_t ms = port_time_ms();
  (void)snprintf(s_time_str, sizeof(s_time_str), "%lu.%03u",
                 (unsigned long)(ms / 1000U), (unsigned)(ms % 1000U));
  return s_time_str;
}

/**
 * @brief 当前进程信息接口（裸机无进程概念，返回空串）
 */
const char *elog_port_get_p_info(void) {
  return "";
}

/**
 * @brief 当前线程信息接口（裸机无线程概念，返回空串）
 */
const char *elog_port_get_t_info(void) {
  return "";
}
