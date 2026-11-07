/**
 ******************************************************************************
 * @file    uart_port_stm32.c
 * @brief   L0 串口抽象 —— STM32F1 实现（huart2 网络通道 / huart1 日志通道）
 ******************************************************************************
 *
 * 适配来源
 *   一级 · 大型开源：ST 官方 STM32CubeF1 的 UART DMA + IDLE 例程
 *                    - HAL_UARTEx_ReceiveToIdle_DMA 启动不定长接收
 *                    - HAL_UARTEx_RxEventCallback 收到数据事件
 *                    - HAL_UART_TxCpltCallback / HAL_UART_ErrorCallback
 *                    - 收到事件后先 HAL_UART_DMAStop() 再取长度（对应 PH2 的
 *                      「必须先关闭 DMA 再计算长度，否则会丢尾字节」风险点）
 *                    - 关闭半传输中断（__HAL_DMA_DISABLE_IT(hdmarx, DMA_IT_HT)），
 *                      避免 HT 事件被误判为一次接收完成 —— 这是 ST 例程里
 *                      ReceiveToIdle 的标准做法
 *   一级 · 大型开源：FreeRTOS-Kernel 互斥量语义（通道锁；PH4 替换实现不改签名）
 *   二级 · 自有模块：旧工程的 iic_DHT20/oled 均为阻塞式使用，无 UART 代码可迁
 *   三级 · 小型开源：未使用
 *
 * 中断归属（零改动 CubeMX 生成文件）：
 *   CubeMX 生成的 stm32f1xx_it.c 中 USART2_IRQHandler() → HAL_UART_IRQHandler()
 *   → 本文件实现的 HAL_UARTEx_RxEventCallback / HAL_UART_ErrorCallback /
 *   HAL_UART_TxCpltCallback；DMA 中断由 CubeMX 生成的 DMA 处理函数转发。
 ******************************************************************************
 */

#include "uart_port.h"

#include "board.h"
#include "main.h"  /* HAL —— 仅 L0 实现文件可包含 */
#include "usart.h" /* CubeMX 生成：extern UART_HandleTypeDef huart1 / huart2 */

/* ==========================================================================
 * 私有状态（按通道索引）
 * ========================================================================== */

typedef struct {
  UART_HandleTypeDef *huart;
  bool tx_busy;               /**< DMA 发送进行中 */
  uart_port_done_cb_t tx_cb;  /**< 发送完成回调 */
  void *tx_arg;               /**< 发送回调上下文 */
  uint32_t lock_depth;        /**< 通道锁嵌套深度 */
  uint32_t last_error;        /**< 最近一次原始错误码 */
} uart_channel_t;

static uart_channel_t s_ch[UART_PORT_CH_MAX] = {
    {&huart2, false, NULL, NULL, 0U, 0U},
#if BOARD_UART_LOG_ENABLE
    {&huart1, false, NULL, NULL, 0U, 0U},
#else
    {NULL, false, NULL, NULL, 0U, 0U},
#endif
};

/** 不定长接收状态（仅网络通道） */
static uint8_t *s_rx_buf = NULL;
static uint16_t s_rx_cap = 0U;
static uart_port_rx_cb_t s_rx_cb = NULL;
static void *s_rx_arg = NULL;
static bool s_rx_active = false;
static uint32_t s_rx_errors = 0U;
static uint32_t s_rx_events = 0U;

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/** @brief 由 HAL 句柄反查通道号；未匹配返回 UART_PORT_CH_MAX */
static uart_port_channel_t channel_of(UART_HandleTypeDef *huart) {
  for (uint32_t i = 0U; i < (uint32_t)UART_PORT_CH_MAX; i++) {
    if ((s_ch[i].huart != NULL) && (s_ch[i].huart == huart)) {
      return (uart_port_channel_t)i;
    }
  }
  return UART_PORT_CH_MAX;
}

static bool channel_valid(uart_port_channel_t ch) {
  return ((uint32_t)ch < (uint32_t)UART_PORT_CH_MAX) && (s_ch[ch].huart != NULL);
}

/** @brief HAL 状态 → 中性状态码（原始错误位由调用方按通道留存） */
static port_status_t map_status(HAL_StatusTypeDef st) {
  switch (st) {
  case HAL_OK:
    return PORT_OK;
  case HAL_BUSY:
    return PORT_BUSY;
  case HAL_TIMEOUT:
    return PORT_TIMEOUT;
  case HAL_ERROR:
  default:
    return PORT_ERROR;
  }
}

/** @brief 可重入通道锁（PH4 换成递归互斥量，签名不变） */
static void channel_lock(uart_port_channel_t ch) {
  port_enter_critical();
  s_ch[ch].lock_depth++;
  port_exit_critical();
}

static void channel_unlock(uart_port_channel_t ch) {
  port_enter_critical();
  if (s_ch[ch].lock_depth > 0U) {
    s_ch[ch].lock_depth--;
  }
  port_exit_critical();
}

/** @brief 启动一次 ReceiveToIdle（并关闭半传输中断），失败时置为非活跃 */
static port_status_t rx_arm(UART_HandleTypeDef *huart) {
  if (HAL_UARTEx_ReceiveToIdle_DMA(huart, s_rx_buf, s_rx_cap) != HAL_OK) {
    s_rx_active = false;
    s_rx_errors++;
    return PORT_ERROR;
  }
  if (huart->hdmarx != NULL) {
    /* 只关心「空闲」与「整缓冲填满」两种事件，关掉半传输事件 */
    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
  }
  return PORT_OK;
}

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

port_status_t uart_port_init(void) {
  if (s_ch[UART_PORT_CH_NET].huart == NULL) {
    return PORT_ERROR;
  }
  if (HAL_UART_GetState(s_ch[UART_PORT_CH_NET].huart) == HAL_UART_STATE_RESET) {
    return PORT_ERROR; /* 未调用 MX_USART2_UART_Init() */
  }
  return PORT_OK;
}

bool uart_port_is_ready(uart_port_channel_t ch) {
  if (!channel_valid(ch)) {
    return false;
  }
  return HAL_UART_GetState(s_ch[ch].huart) != HAL_UART_STATE_RESET;
}

uint32_t uart_port_baudrate(uart_port_channel_t ch) {
  if (!channel_valid(ch)) {
    return 0U;
  }
  return s_ch[ch].huart->Init.BaudRate;
}

/* ==========================================================================
 * 发送
 * ========================================================================== */

port_status_t uart_port_write(uart_port_channel_t ch, const uint8_t *data,
                              uint16_t len, uint32_t timeout_ms) {
  if (!channel_valid(ch) || (data == NULL) || (len == 0U) ||
      (timeout_ms == 0U)) {
    return PORT_ERROR;
  }
  channel_lock(ch);
  const port_status_t st = map_status(
      HAL_UART_Transmit(s_ch[ch].huart, (uint8_t *)data, len, timeout_ms));
  if (st != PORT_OK) {
    s_ch[ch].last_error = HAL_UART_GetError(s_ch[ch].huart);
  }
  channel_unlock(ch);
  return st;
}

port_status_t uart_port_write_dma(uart_port_channel_t ch, const uint8_t *data,
                                  uint16_t len, uart_port_done_cb_t cb,
                                  void *arg) {
  if (!channel_valid(ch) || (data == NULL) || (len == 0U)) {
    return PORT_ERROR;
  }
  if (s_ch[ch].tx_busy) {
    return PORT_BUSY;
  }

  UART_HandleTypeDef *huart = s_ch[ch].huart;
  s_ch[ch].tx_cb = cb;
  s_ch[ch].tx_arg = arg;

  /* DMA 未配置时回退轮询（对上层透明），并同步触发回调 */
  if (huart->hdmatx == NULL) {
    const port_status_t st = uart_port_write(ch, data, len, APP_UART_TX_TIMEOUT_MS);
    uart_port_done_cb_t done = s_ch[ch].tx_cb;
    void *done_arg = s_ch[ch].tx_arg;
    s_ch[ch].tx_cb = NULL;
    s_ch[ch].tx_arg = NULL;
    if (done != NULL) {
      done(st, done_arg);
    }
    return PORT_OK;
  }

  s_ch[ch].tx_busy = true;
  if (HAL_UART_Transmit_DMA(huart, (uint8_t *)data, len) != HAL_OK) {
    s_ch[ch].tx_busy = false;
    s_ch[ch].tx_cb = NULL;
    s_ch[ch].tx_arg = NULL;
    s_ch[ch].last_error = HAL_UART_GetError(huart);
    return PORT_ERROR;
  }
  return PORT_OK;
}

bool uart_port_tx_busy(uart_port_channel_t ch) {
  if (!channel_valid(ch)) {
    return false;
  }
  if (s_ch[ch].tx_busy) {
    return true;
  }
  const HAL_UART_StateTypeDef st = HAL_UART_GetState(s_ch[ch].huart);
  return (st & HAL_UART_STATE_BUSY_TX) != 0U;
}

/* ==========================================================================
 * 不定长接收（网络通道，DMA + IDLE）
 * ========================================================================== */

port_status_t uart_port_rx_start(uint8_t *buf, uint16_t cap,
                                 uart_port_rx_cb_t cb, void *arg) {
  if ((buf == NULL) || (cap == 0U) || (cb == NULL) ||
      !uart_port_is_ready(UART_PORT_CH_NET)) {
    return PORT_ERROR;
  }
  if (s_rx_active) {
    return PORT_BUSY;
  }

  s_rx_buf = buf;
  s_rx_cap = cap;
  s_rx_cb = cb;
  s_rx_arg = arg;
  s_rx_active = true;

  const port_status_t st = rx_arm(s_ch[UART_PORT_CH_NET].huart);
  if (st != PORT_OK) {
    s_rx_buf = NULL;
    s_rx_cap = 0U;
    s_rx_cb = NULL;
    s_rx_arg = NULL;
  }
  return st;
}

port_status_t uart_port_rx_stop(void) {
  if (!s_rx_active) {
    return PORT_OK;
  }
  const HAL_StatusTypeDef st = HAL_UART_AbortReceive(s_ch[UART_PORT_CH_NET].huart);
  s_rx_active = false;
  s_rx_buf = NULL;
  s_rx_cap = 0U;
  s_rx_cb = NULL;
  s_rx_arg = NULL;
  return (st == HAL_OK) ? PORT_OK : PORT_ERROR;
}

bool uart_port_rx_is_active(void) { return s_rx_active; }

/* ==========================================================================
 * HAL 回调出口
 * ========================================================================== */

/**
 * @brief 接收事件（空闲超时 / 缓冲填满 / 半传输）
 * @param huart 触发事件的串口
 * @param Size  本次**已接收**的字节数（HAL 在关闭 DMA 之前算好的值）
 * @note  处理顺序严格按 PH2 风险点要求：**先停 DMA 再取长度**，
 *        取完长度立即重启接收，全程非阻塞、不丢尾字节。
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
  if (huart != s_ch[UART_PORT_CH_NET].huart) {
    return;
  }
  if (!s_rx_active) {
    return;
  }

  /* ① 先关闭 DMA，确保 Size 之后不会再有字节被搬进缓冲区 */
  (void)HAL_UART_DMAStop(huart);

  /* ② 再判定长度并投递（Size 由 HAL 在停 DMA 前计算，语义为「已收字节数」） */
  if ((Size > 0U) && (Size <= s_rx_cap)) {
    s_rx_events++;
    if (s_rx_cb != NULL) {
      s_rx_cb(s_rx_buf, Size, s_rx_arg);
    }
  } else if (Size > s_rx_cap) {
    s_rx_errors++; /* 异常：长度不可能超过容量，留痕便于回归 */
  }

  /* ③ 立即重启接收（调用方无需重复启动） */
  if (s_rx_active) {
    (void)rx_arm(huart);
  }
}

/** @brief 串口错误（溢出 ORE / 帧错误 FE / 噪声 NE / DMA 错误）：计数并重启接收 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
  const uart_port_channel_t ch = channel_of(huart);
  if (ch >= UART_PORT_CH_MAX) {
    return;
  }

  s_ch[ch].last_error = HAL_UART_GetError(huart);
  s_ch[ch].tx_busy = false;
  s_ch[ch].tx_cb = NULL;
  s_ch[ch].tx_arg = NULL;

  if ((ch == UART_PORT_CH_NET) && s_rx_active) {
    s_rx_errors++;
    /* 错误后必须重新启动，否则接收会永久停摆（PH2 长稳测试的关键） */
    if (rx_arm(huart) != PORT_OK) {
      s_rx_buf = NULL;
      s_rx_cap = 0U;
      s_rx_cb = NULL;
      s_rx_arg = NULL;
    }
  }
}

/** @brief DMA 发送完成：清忙标志并在锁外通知上层 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
  const uart_port_channel_t ch = channel_of(huart);
  if (ch >= UART_PORT_CH_MAX) {
    return;
  }

  s_ch[ch].tx_busy = false;
  const uart_port_done_cb_t cb = s_ch[ch].tx_cb;
  void *const arg = s_ch[ch].tx_arg;
  s_ch[ch].tx_cb = NULL;
  s_ch[ch].tx_arg = NULL;

  if (cb != NULL) {
    cb(PORT_OK, arg);
  }
}

/* ==========================================================================
 * 总线锁 / 诊断
 * ========================================================================== */

void uart_port_lock(uart_port_channel_t ch) {
  if (channel_valid(ch)) {
    channel_lock(ch);
  }
}

void uart_port_unlock(uart_port_channel_t ch) {
  if (channel_valid(ch)) {
    channel_unlock(ch);
  }
}

uint32_t uart_port_lock_depth(uart_port_channel_t ch) {
  return channel_valid(ch) ? s_ch[ch].lock_depth : 0U;
}

uint32_t uart_port_rx_error_count(void) { return s_rx_errors; }

uint32_t uart_port_rx_event_count(void) { return s_rx_events; }

uint32_t uart_port_last_error(uart_port_channel_t ch) {
  return channel_valid(ch) ? s_ch[ch].last_error : 0U;
}
