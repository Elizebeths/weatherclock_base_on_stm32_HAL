/**
 ******************************************************************************
 * @file    spi_port.h
 * @brief   L0 SPI 总线抽象：主机收发（轮询 / DMA）、超时、总线锁
 *
 * @note    分层位置：L0（Ports/bus）。本文件是本工程 SPI 外设的**唯一持有者**：
 *          Ports/display/lcd_port 通过本接口搬运像素，后续 PH 的
 *          Devices/storage/w25qxx（预留扩展位）复用同一接口 ——
 *          因此不会出现两个模块争抢同一个 hspi1 的问题。
 *          本文件不得出现 HAL / STM32 符号。
 ******************************************************************************
 *
 * 适配来源
 *   一级 · 大型开源：ST 官方 STM32CubeF1 的 SPI DMA 例程
 *                    （HAL_SPI_Transmit / HAL_SPI_Transmit_DMA +
 *                      传输完成回调、HAL_SPI_GetState 判忙、
 *                      HAL_SPI_Abort 异常中止）
 *   二级 · 自有模块：lcd.c 的 LCD_transfer_() 与 LCD_writeDATStream_()
 *                    —— 后者的「按 4096 B 分块、CS 由上层管理」策略被
 *                    抽象为这里的自动分块（CS 交给 lcd_port）
 *   一级 · 大型开源：FreeRTOS-Kernel 的互斥量语义（总线锁接口形态），
 *                    PH4 起在保持签名不变的前提下换为 xSemaphoreTake
 *   三级 · 小型开源：未使用
 ******************************************************************************
 */

#ifndef SPI_PORT_H
#define SPI_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 以 Ports/ 为根：只需 -I Ports 与 -I Config；依赖方向 总线 → 公共，单向 */
#include "common/port.h"

/* ==========================================================================
 * 回调类型
 * ========================================================================== */

/**
 * @brief DMA 传输完成回调
 * @param status PORT_OK / PORT_TIMEOUT / PORT_ERROR
 * @param arg    注册时传入的上下文
 * @note  在中断上下文（DMA 完成）或同步返回（DMA 不可用而回退轮询）时调用。
 */
typedef void (*spi_port_done_cb_t)(port_status_t status, void *arg);

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

/**
 * @brief 确认 SPI 外设可用（幂等）
 * @retval PORT_OK    hspi1 处于就绪状态（由 CubeMX 的 MX_SPI1_Init() 初始化）
 * @retval PORT_ERROR SPI 外设未初始化，或工程中未启用 SPI1
 * @note  本 Port 不自行调用 HAL_SPI_Init —— 外设配置归 CubeMX，符合
 *        「生成文件只保留初始化角色」的分层约定。
 */
port_status_t spi_port_init(void);

/** @brief SPI 外设是否就绪（供 L1 在初始化编排中做前置检查） */
bool spi_port_is_ready(void);

/* ==========================================================================
 * 传输
 * ========================================================================== */

/**
 * @brief 轮询发送（阻塞）
 * @param data       数据指针
 * @param len        字节数；内部按 APP_SPI_LCD_CHUNK_BYTES 自动分块
 * @param timeout_ms 单块的超时（毫秒）；禁止传 HAL_MAX_DELAY 语义的无限值
 * @retval PORT_OK / PORT_TIMEOUT / PORT_ERROR
 * @note  即 DMA 不可用时的降级路径，与 DMA 路径行为等价。
 */
port_status_t spi_port_tx(const uint8_t *data, uint16_t len, uint32_t timeout_ms);

/**
 * @brief 全双工收发（轮询）
 * @param tx 发送缓冲；可为 NULL（此时只收）
 * @param rx 接收缓冲；可为 NULL（此时只发）
 * @param len 字节数
 * @param timeout_ms 超时（毫秒）
 * @note  当前 TFT 走 TX-Only 单工，本接口为后续需要 MISO 的器件预留。
 */
port_status_t spi_port_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len,
                            uint32_t timeout_ms);

/**
 * @brief 以 DMA 发送（非阻塞，一次一块）
 * @param data 数据指针（**回调返回前不得改写**）
 * @param len  字节数，须 <= 65535（HAL_SPI_Transmit_DMA 的 Size 为 uint16_t）
 * @param cb   完成回调，可为 NULL
 * @param arg  回调上下文
 * @retval PORT_OK    已提交
 * @retval PORT_BUSY  上一次 DMA 尚未完成
 * @retval PORT_ERROR len 超限 / 指针为空 / 外设未就绪
 * @note  若工程未配置 SPI1_TX 的 DMA（hspi1.hdmatx == NULL），自动回退为
 *        轮询并在返回前**同步**触发回调，对上层完全透明。
 */
port_status_t spi_port_tx_dma(const uint8_t *data, uint32_t len,
                              spi_port_done_cb_t cb, void *arg);

/** @brief 是否有传输（轮询或 DMA）正在进行 */
bool spi_port_busy(void);

/** @brief 中止正在进行的 DMA 传输 */
port_status_t spi_port_abort(void);

/* ==========================================================================
 * 总线锁
 * ========================================================================== */

/**
 * @brief 获取总线锁（可重入，带超时）
 * @note  裸机实现为「嵌套计数 + 极短临界区改计数 + 自旋等待」，
 *        **不会长时间关中断**，因此可以安全地跨一次异步 DMA 传输持有；
 *        PH4 引入 FreeRTOS 后替换为递归互斥量，签名不变。
 */
void spi_port_lock(void);

/** @brief 释放总线锁（与 spi_port_lock() 配对） */
void spi_port_unlock(void);

/** @brief 当前锁嵌套深度（0 = 未被持有，诊断用） */
uint32_t spi_port_lock_depth(void);

/* ==========================================================================
 * 诊断
 * ========================================================================== */

/** @brief 当前实际 SPI 时钟频率（Hz），由 APB2 时钟与预分频推导 */
uint32_t spi_port_clock_hz(void);

/** @brief 上一次失败的原始错误码（0 表示无；仅用于日志定位，非 HAL 类型） */
uint32_t spi_port_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* SPI_PORT_H */
