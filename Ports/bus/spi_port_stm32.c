/**
 ******************************************************************************
 * @file    spi_port_stm32.c
 * @brief   L0 SPI 总线抽象 —— STM32F1 实现（hspi1 的唯一持有者）
 ******************************************************************************
 *
 * 适配来源
 *   一级 · 大型开源：ST 官方 STM32CubeF1 的 SPI DMA 例程与 HAL SPI 状态机
 *                    （HAL_SPI_Transmit / HAL_SPI_Transmit_DMA /
 *                      HAL_SPI_GetState / HAL_SPI_Abort /
 *                      HAL_SPI_TxCpltCallback / HAL_SPI_ErrorCallback）
 *   一级 · 大型开源：FreeRTOS-Kernel 互斥量语义（总线锁；PH4 替换实现不改签名）
 *   二级 · 自有模块：lcd.c 的 LCD_transfer_() 超时策略（100 ms，非 HAL_MAX_DELAY）
 *                    与 LCD_writeDATStream_() 的 4096 B 分块
 *   三级 · 小型开源：未使用
 *
 * DMA 归属：SPI1_TX 的 DMA 句柄由 CubeMX 生成并在 HAL_SPI_MspInit() 中
 * 用 __HAL_LINKDMA(hspi, hdmatx, hdma_spi1_tx) 链接到 hspi1。本层不自己创建
 * DMA 句柄，只调用 HAL_SPI_Transmit_DMA 并实现完成/错误回调 ——
 * 这样「外设与 DMA 配置」全部留在 CubeMX，符合分层约定。
 ******************************************************************************
 */

#include "spi_port.h"

#include "board.h"
#include "main.h" /* HAL —— 仅 L0 实现文件可包含 */
#include "spi.h"  /* CubeMX 生成：extern SPI_HandleTypeDef hspi1 */

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

/** 本层独占使用 SPI1。若将来 SPI1 挂多个从机，由上层用总线锁串行化。 */
static SPI_HandleTypeDef *const s_spi = &hspi1;

static bool s_dma_pending = false;
static spi_port_done_cb_t s_done_cb = NULL;
static void *s_done_arg = NULL;
static uint32_t s_last_error = 0U;

/** 总线锁：可重入嵌套计数（裸机下不会长时间关中断，故可跨异步传输持有） */
static uint32_t s_lock_depth = 0U;

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/**
 * @brief 获取总线锁（裸机实现：可重入嵌套计数，极短临界区保护计数）
 * @note  裸机结构是「单主循环 + 中断」，不存在两个线程同时抢总线的情形；
 *        唯一的复用点是「在 DMA 完成回调里重入提交下一块」，因此这里采用
 *        可重入的嵌套计数，而不是自旋等待。
 *        优点：**不会长时间关中断**，可以安全地跨一次异步 DMA 传输持有。
 *        PH4 引入 FreeRTOS 后，本函数体替换为
 *        xSemaphoreTakeRecursive(…, pdMS_TO_TICKS(APP_BUS_LOCK_TIMEOUT_MS))，
 *        函数签名与调用点一行不改。
 */
static void bus_lock(void) {
  port_enter_critical();
  s_lock_depth++;
  port_exit_critical();
}

static void bus_unlock(void) {
  port_enter_critical();
  if (s_lock_depth > 0U) {
    s_lock_depth--;
  }
  port_exit_critical();
}

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

port_status_t spi_port_init(void) {
  /* 只做就绪校验：SPI1 的配置（含 DMA 链接）由 CubeMX 的 MX_SPI1_Init() 完成 */
  if (s_spi->Instance != SPI1) {
    return PORT_ERROR;
  }
  return (HAL_SPI_GetState(s_spi) == HAL_SPI_STATE_RESET) ? PORT_ERROR : PORT_OK;
}

bool spi_port_is_ready(void) {
  return (s_spi->Instance == SPI1) &&
         (HAL_SPI_GetState(s_spi) != HAL_SPI_STATE_RESET);
}

/* ==========================================================================
 * 轮询发送
 * ========================================================================== */

port_status_t spi_port_tx(const uint8_t *data, uint16_t len,
                          uint32_t timeout_ms) {
  if ((data == NULL) || !spi_port_is_ready()) {
    return PORT_ERROR;
  }
  if (len == 0U) {
    return PORT_OK;
  }

  uint32_t remaining = len;
  const uint8_t *p = data;

  while (remaining > 0U) {
    const uint16_t chunk = (remaining > APP_SPI_LCD_CHUNK_BYTES)
                               ? (uint16_t)APP_SPI_LCD_CHUNK_BYTES
                               : (uint16_t)remaining;

    if (HAL_SPI_Transmit(s_spi, (uint8_t *)p, chunk, timeout_ms) != HAL_OK) {
      s_last_error = HAL_SPI_GetError(s_spi);
      return PORT_TIMEOUT;
    }
    p += chunk;
    remaining -= chunk;
  }
  return PORT_OK;
}

port_status_t spi_port_txrx(const uint8_t *tx, uint8_t *rx, uint16_t len,
                            uint32_t timeout_ms) {
  if (!spi_port_is_ready() || ((tx == NULL) && (rx == NULL)) || (len == 0U)) {
    return PORT_ERROR;
  }
  if (HAL_SPI_TransmitReceive(s_spi, (uint8_t *)tx, rx, len, timeout_ms) !=
      HAL_OK) {
    s_last_error = HAL_SPI_GetError(s_spi);
    return PORT_TIMEOUT;
  }
  return PORT_OK;
}

/* ==========================================================================
 * DMA 发送（一次一块）
 * ========================================================================== */

port_status_t spi_port_tx_dma(const uint8_t *data, uint32_t len,
                              spi_port_done_cb_t cb, void *arg) {
  if ((data == NULL) || (len == 0U) || (len > 0xFFFFU) || !spi_port_is_ready()) {
    return PORT_ERROR;
  }
  if (s_dma_pending) {
    return PORT_BUSY;
  }

  s_done_cb = cb;
  s_done_arg = arg;

  /* DMA 未配置（hspi1.hdmatx 为空）时退回轮询：对上层透明，
   * 同时保留「性能优化不牺牲可用性」的降级路径（PH1 S3 的要求）。 */
  if (s_spi->hdmatx == NULL) {
    const port_status_t st = spi_port_tx(data, (uint16_t)len, APP_SPI_LCD_TIMEOUT_MS);
    spi_port_done_cb_t done = s_done_cb;
    void *done_arg = s_done_arg;
    s_done_cb = NULL;
    s_done_arg = NULL;
    if (done != NULL) {
      done(st, done_arg);
    }
    return PORT_OK;
  }

  s_dma_pending = true;
  if (HAL_SPI_Transmit_DMA(s_spi, (uint8_t *)data, (uint16_t)len) != HAL_OK) {
    s_dma_pending = false;
    s_last_error = HAL_SPI_GetError(s_spi);
    s_done_cb = NULL;
    s_done_arg = NULL;
    return PORT_ERROR;
  }
  return PORT_OK;
}

bool spi_port_busy(void) {
  if (s_dma_pending) {
    return true;
  }
  const HAL_SPI_StateTypeDef st = HAL_SPI_GetState(s_spi);
  return (st != HAL_SPI_STATE_READY) && (st != HAL_SPI_STATE_RESET);
}

port_status_t spi_port_abort(void) {
  if (!s_dma_pending) {
    return PORT_OK;
  }
  const HAL_StatusTypeDef st = HAL_SPI_Abort(s_spi);
  s_dma_pending = false;
  s_done_cb = NULL;
  s_done_arg = NULL;
  return (st == HAL_OK) ? PORT_OK : PORT_ERROR;
}

/* ==========================================================================
 * HAL 回调出口
 * ========================================================================== */

/** @brief DMA 发送完成：清除忙标志并通知上层（在中断上下文执行） */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
  if (hspi != s_spi) {
    return;
  }
  s_dma_pending = false;
  const spi_port_done_cb_t cb = s_done_cb;
  void *const arg = s_done_arg;
  s_done_cb = NULL;
  s_done_arg = NULL;
  if (cb != NULL) {
    cb(PORT_OK, arg);
  }
}

/** @brief SPI 错误：置位错误码并以 PORT_ERROR 通知上层 */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
  if (hspi != s_spi) {
    return;
  }
  s_last_error = HAL_SPI_GetError(hspi);
  s_dma_pending = false;
  const spi_port_done_cb_t cb = s_done_cb;
  void *const arg = s_done_arg;
  s_done_cb = NULL;
  s_done_arg = NULL;
  if (cb != NULL) {
    cb(PORT_ERROR, arg);
  }
}

/* ==========================================================================
 * 总线锁 / 诊断
 * ========================================================================== */

void spi_port_lock(void) { bus_lock(); }

void spi_port_unlock(void) { bus_unlock(); }

uint32_t spi_port_lock_depth(void) { return s_lock_depth; }

uint32_t spi_port_clock_hz(void) {
  /* HAL 不直接给出实际时钟，由 APB2 频率与预分频推导：
   * SPI_BAUDRATEPRESCALER_2 = 0 … _256 = 7，恰好是 2 的幂次指数 */
  const uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
  const uint32_t shift = (uint32_t)s_spi->Init.BaudRatePrescaler;
  return (shift <= 7U) ? (pclk2 >> shift) : pclk2;
}

uint32_t spi_port_last_error(void) { return s_last_error; }
