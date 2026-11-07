/**
 ******************************************************************************
 * @file    uart_port.h
 * @brief   L0 串口抽象：两路通道（网络 AT + 分级日志）、DMA + IDLE 不定长接收
 *
 * @note    分层位置：L0（Ports/bus）。PH2 专题把「UART2 + DMA + IDLE 空闲中断
 *          实现不定长接收」列为本阶段技术核心；本文件即该能力的 L0 落点。
 *          本文件不得出现 HAL / STM32 符号。
 ******************************************************************************
 *
 * 适配来源
 *   一级 · 大型开源：ST 官方 STM32CubeF1 的 UART DMA + IDLE 例程
 *                    （HAL_UARTEx_ReceiveToIdle_DMA + HAL_UARTEx_RxEventCallback）
 *   一级 · 大型开源：FreeRTOS-Kernel 的互斥量语义（总线锁与回调形态）
 *   二级 · 自有模块：旧工程未使用 UART，无自有代码可迁；接口按 PH1/PH2 需求定义
 *   三级 · 小型开源：未使用
 *
 * 📌 关于「两路通道」的说明（对实施计划的一处必要补全）：
 *   PH1 专题把日志放在 L2 的 Services/log.c（纯逻辑、不得接触 HAL），却只把
 *   Ports/bus 的 6 个文件给了通信模块。于是「日志往哪里写」在 L0 没有落点。
 *   本实现因此把 **USART1 作为日志通道** 一并纳入 uart_port（不新增文件，
 *   仍为 15 个 Port 文件）：L2 的 log.c 只需调用
 *   uart_port_write(UART_PORT_CH_LOG, …) 即可，无需任何 HAL 符号。
 *   网络通道（USART2）支持 DMA + IDLE 不定长接收（当前无设备使用）；
 *   日志通道为单向发送，不支持接收。
 *
 * ⚠ PH2 风险点的对应处理（实施计划原文：「IDLE 中断与 DMA 的配合：必须先关闭
 *   DMA 再计算长度，否则会丢尾字节」）：
 *   收到 HAL_UARTEx_RxEventCallback 后先 HAL_UART_DMAStop() 停掉 DMA，再用 HAL
 *   给出的 Size（本次已收字节数）判定长度，随后立即重启接收 —— 全程非阻塞，
 *   不丢尾字节。
 *
 * ⚠ 前置条件：工程须在 CubeMX 中启用 USART2 与它的 TX/RX DMA 通道（见
 *   Config/board.h 的配置清单）。若未配置 DMA，本 Port 自动回退为轮询收发，
 *   功能可用但会阻塞 CPU（PH2 的性能目标要求必须开 DMA）。
 ******************************************************************************
 */

#ifndef UART_PORT_H
#define UART_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 以 Ports/ 为根：只需 -I Ports 与 -I Config；依赖方向 总线 → 公共，单向 */
#include "common/port.h"

/* ==========================================================================
 * 通道
 * ========================================================================== */

typedef enum {
  UART_PORT_CH_NET = 0, /**< USART2：网络通道（双向，DMA + IDLE，当前无设备使用） */
  UART_PORT_CH_LOG,     /**< USART1：分级日志（单向发送） */
  UART_PORT_CH_MAX
} uart_port_channel_t;

/* ==========================================================================
 * 回调类型
 * ========================================================================== */

/**
 * @brief 不定长接收回调（仅网络通道）
 * @param data 本次收到的数据（指向用户提供的缓冲区，回调返回后即可复用）
 * @param len  本次收到的字节数（> 0）
 * @param arg  注册时传入的上下文
 * @note  在中断上下文执行：不得阻塞、不得调用非 FromISR 的 RTOS API。
 */
typedef void (*uart_port_rx_cb_t)(const uint8_t *data, uint16_t len, void *arg);

/**
 * @brief DMA 发送完成回调
 * @param status PORT_OK / PORT_ERROR
 * @param arg    注册时传入的上下文
 */
typedef void (*uart_port_done_cb_t)(port_status_t status, void *arg);

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

/**
 * @brief 确认两个串口通道可用（幂等）
 * @retval PORT_OK    网络通道就绪
 * @retval PORT_ERROR 未启用 USART2（网络通道是必经之路）
 * @note  日志通道（USART1）缺失只影响日志，不影响返回值；可用
 *        uart_port_is_ready(UART_PORT_CH_LOG) 单独查询。
 */
port_status_t uart_port_init(void);

/** @brief 指定通道是否就绪 */
bool uart_port_is_ready(uart_port_channel_t ch);

/** @brief 指定通道的波特率（Hz），未就绪返回 0 */
uint32_t uart_port_baudrate(uart_port_channel_t ch);

/* ==========================================================================
 * 发送
 * ========================================================================== */

/** @brief 轮询发送（阻塞，带毫秒超时） */
port_status_t uart_port_write(uart_port_channel_t ch, const uint8_t *data,
                              uint16_t len, uint32_t timeout_ms);

/**
 * @brief DMA 发送（非阻塞，一次一块）
 * @param data 数据指针（**回调返回前不得改写**）
 * @param len  字节数（<= 65535）
 * @param cb   完成回调，可为 NULL
 * @retval PORT_BUSY 上一次发送尚未完成
 * @note  DMA 不可用时回退轮询并同步触发回调。
 */
port_status_t uart_port_write_dma(uart_port_channel_t ch, const uint8_t *data,
                                  uint16_t len, uart_port_done_cb_t cb,
                                  void *arg);

/** @brief 指定通道是否有发送正在进行 */
bool uart_port_tx_busy(uart_port_channel_t ch);

/* ==========================================================================
 * 不定长接收（仅网络通道；DMA + IDLE）
 * ========================================================================== */

/**
 * @brief 启动不定长接收
 * @param buf 接收缓冲区（调用方长期持有；DMA 会直接写入，回调返回前不得复用）
 * @param cap 缓冲区容量（字节）
 * @param cb  每次「一段时间无新数据」或「缓冲区填满」时触发
 * @param arg 回调上下文
 * @retval PORT_OK / PORT_ERROR / PORT_BUSY（已在接收中）
 * @note  回调返回后实现会自动重启接收，调用方无需重复启动。
 */
port_status_t uart_port_rx_start(uint8_t *buf, uint16_t cap,
                                 uart_port_rx_cb_t cb, void *arg);

/** @brief 停止不定长接收 */
port_status_t uart_port_rx_stop(void);

/** @brief 是否处于接收中 */
bool uart_port_rx_is_active(void);

/* ==========================================================================
 * 总线锁与诊断
 * ========================================================================== */

/** @brief 获取指定通道的锁（可重入，带超时） */
void uart_port_lock(uart_port_channel_t ch);

/** @brief 释放指定通道的锁 */
void uart_port_unlock(uart_port_channel_t ch);

/** @brief 指定通道的锁嵌套深度（0 = 未被持有） */
uint32_t uart_port_lock_depth(uart_port_channel_t ch);

/**
 * @brief 接收溢出/错误累计次数
 * @note  PH2 的验收要求「连续运行 2 小时无丢包」需要这个计数作为证据，
 *        也是「丢尾字节」问题的回归判定依据。
 */
uint32_t uart_port_rx_error_count(void);

/** @brief 成功完成的接收事件次数（每次触发回调计一次） */
uint32_t uart_port_rx_event_count(void);

/** @brief 指定通道上一次失败的原始错误码（0 = 无；HAL_UART_ERROR_* 位组合的中性副本） */
uint32_t uart_port_last_error(uart_port_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif /* UART_PORT_H */
