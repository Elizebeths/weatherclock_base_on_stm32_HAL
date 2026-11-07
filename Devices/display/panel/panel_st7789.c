/**
 ******************************************************************************
 * @file    panel_st7789.c
 * @brief   L1 驱动层实现：ST7789 面板（序列表初始化、窗口、DMA 像素提交）
 *
 * @note    提交策略：像素流按 DISPLAY_DMA_BLOCK_MAX 分块，逐块 DMA 提交并在
 *          信号量上等待完成。**不在中断回调里链式提交**——回调只置状态并给出
 *          信号量，由等待方继续下一块。这样避免了「中断上下文递归提交」与
 *          「同步回退（未配置 DMA）导致的深递归」两个风险点。
 *
 * @note    阻塞语义：panel_blit / panel_fill 会阻塞调用方直到本块数据传完。
 *          RTOS 下阻塞的是显示任务，其他任务照常运行；裸机下阻塞主循环，
 *          与旧实现的轮询语义等价。
 ******************************************************************************
 */

#include "display/panel/panel_st7789.h"

#include <stddef.h>

#include "common/os_port.h"
#include "common/port.h"
#include "display/lcd_port.h"
#include "display/panel/st7789_regs.h"

/** 单次块传输的等待上限（毫秒）。整屏填充约 34~68 ms，留足余量。 */
#define PANEL_TX_TIMEOUT_MS 1000U

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

/** 提交描述：把「数据 / 总量 / 单块大小 / 是否重复」打包，供分块循环复用 */
typedef struct {
  const uint8_t *data; /**< 数据起点 */
  uint32_t total;      /**< 总字节数（偶数字节，像素对齐） */
  uint32_t sent;       /**< 已提交字节数 */
  uint32_t block;      /**< 单块字节数上限 */
  bool repeat;         /**< true = 每块都从 data 起始取（纯色填充） */
  bool active;         /**< 是否有提交在进行 */
} panel_tx_t;

/** 纯色填充用的行/整块缓冲（构造一次、重复提交） */
static uint8_t s_fill_buf[DISPLAY_WIDTH * DISPLAY_FILL_LINES * DISPLAY_BPP];

static panel_tx_t s_tx;
static os_sem_t s_tx_sem;
static os_mutex_t s_lock;
static volatile port_status_t s_block_status = PORT_OK;
static lcd_err_t s_last_err = LCD_OK;
static bool s_inited = false;

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

static lcd_err_t panel_map_(port_status_t st) {
  switch (st) {
  case PORT_OK:
    return LCD_OK;
  case PORT_BUSY:
    return LCD_ERR_BUSY;
  case PORT_TIMEOUT:
  case PORT_ERROR:
  default:
    return LCD_ERR_BUS;
  }
}

/** @brief 单块传输完成（中断上下文或轮询同步回退时被调用）：只记状态 + 给信号量 */
static void panel_block_done_(port_status_t status, void *arg) {
  (void)arg;
  s_block_status = status;
  /* 回调可能运行在中断上下文，必须使用 FromISR 语义的给信号量 */
  os_port_sem_give_isr(&s_tx_sem);
}

/**
 * @brief 按描述符分块提交，逐块等待完成
 * @note  每块提交前先清空信号量，因此不会误消费上一块的计数。
 *        若 lcd_port 因未配置 DMA 而回退轮询，回调会同**同步**返回，
 *        信号量在此之前已被置位，take 立即成功 —— 不会递归、不会死等。
 */
static lcd_err_t panel_tx_run_(void) {
  panel_tx_t *const tx = &s_tx;
  lcd_err_t err = LCD_OK;

  while (tx->sent < tx->total) {
    const uint32_t remaining = tx->total - tx->sent;
    const uint32_t n = (remaining > tx->block) ? tx->block : remaining;
    const uint8_t *p = tx->repeat ? tx->data : (tx->data + tx->sent);

    (void)os_port_sem_take(&s_tx_sem, 0U); /* 清空残留计数 */
    s_block_status = PORT_ERROR;

    const port_status_t st = lcd_port_spi_tx_dma(p, n, panel_block_done_, NULL);
    if (st != PORT_OK) {
      err = panel_map_(st);
      break;
    }

    if (!os_port_sem_take(&s_tx_sem, PANEL_TX_TIMEOUT_MS)) {
      err = LCD_ERR_BUSY; /* 超时：视为提交失败，交由上层决定是否重试 */
      break;
    }
    if (s_block_status != PORT_OK) {
      err = panel_map_(s_block_status);
      break;
    }
    tx->sent += n;
  }

  tx->active = false;
  s_last_err = err;
  return err;
}

/** @brief 把 16 位值写成「高字节在前」的两个字节 */
static void panel_put_u16_(uint8_t *out, uint16_t v) {
  out[0] = (uint8_t)(v >> 8U);
  out[1] = (uint8_t)v;
}

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

lcd_err_t panel_init(void) {
  os_port_mutex_init(&s_lock);
  os_port_sem_init(&s_tx_sem, 0U, 1U);
  s_tx.active = false;
  s_last_err = LCD_OK;

  const port_status_t st = lcd_port_init();
  s_inited = (st == PORT_OK);
  return panel_map_(st);
}

lcd_err_t panel_start(void) {
  if (!s_inited) {
    return LCD_ERR_STATE;
  }

  /* 硬件复位：拉低 ST7789_RESET_LOW_MS → 拉高并等待稳定 */
  lcd_port_pin_write(LCD_PIN_RST, false);
  lcd_port_delay_ms(ST7789_RESET_LOW_MS);
  lcd_port_pin_write(LCD_PIN_RST, true);
  lcd_port_delay_ms(ST7789_RESET_SETTLE_MS);

  for (uint16_t i = 0U; i < ST7789_INIT_SEQ_LEN; i++) {
    const st7789_init_step_t *const step = &ST7789_INIT_SEQ[i];
    if (step->cmd != ST7789_SEQ_DELAY_ONLY) {
      if (lcd_port_cmd_dat(step->cmd, step->dat, (uint16_t)step->len) != PORT_OK) {
        s_last_err = LCD_ERR_BUS;
        return s_last_err;
      }
    }
    if (step->delay_ms != 0U) {
      lcd_port_delay_ms((uint32_t)step->delay_ms);
    }
  }

  s_last_err = LCD_OK;
  return s_last_err;
}

/* ==========================================================================
 * 窗口
 * ========================================================================== */

lcd_err_t panel_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  if ((w == 0U) || (h == 0U) || ((uint32_t)x + w > LCD_WIDTH) ||
      ((uint32_t)y + h > LCD_HEIGHT)) {
    return LCD_ERR_PARAM;
  }

  /* 面板显存偏移只在这里叠加：上层永远用可见区坐标 */
  const uint16_t xs = (uint16_t)(x + DISPLAY_PANEL_COL_OFFSET);
  const uint16_t ys = (uint16_t)(y + DISPLAY_PANEL_ROW_OFFSET);
  const uint16_t xe = (uint16_t)(xs + w - 1U);
  const uint16_t ye = (uint16_t)(ys + h - 1U);

  uint8_t col[4];
  uint8_t row[4];
  panel_put_u16_(&col[0], xs);
  panel_put_u16_(&col[2], xe);
  panel_put_u16_(&row[0], ys);
  panel_put_u16_(&row[2], ye);

  if (lcd_port_cmd_dat(ST7789_CASET, col, 4U) != PORT_OK) {
    return LCD_ERR_BUS;
  }
  if (lcd_port_cmd_dat(ST7789_RASET, row, 4U) != PORT_OK) {
    return LCD_ERR_BUS;
  }
  if (lcd_port_cmd_dat(ST7789_RAMWR, NULL, 0U) != PORT_OK) {
    return LCD_ERR_BUS;
  }
  return LCD_OK;
}

/* ==========================================================================
 * 像素提交
 * ========================================================================== */

lcd_err_t panel_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     const uint8_t *rgb565) {
  if ((rgb565 == NULL) || (w == 0U) || (h == 0U)) {
    return LCD_ERR_PARAM;
  }
  if (!os_port_mutex_lock(&s_lock, PANEL_TX_TIMEOUT_MS)) {
    return LCD_ERR_BUSY;
  }

  lcd_err_t err = panel_set_window(x, y, w, h);
  if (err == LCD_OK) {
    const uint32_t bytes = (uint32_t)w * (uint32_t)h * DISPLAY_BPP;
    s_tx.data = rgb565;
    s_tx.total = bytes;
    s_tx.sent = 0U;
    s_tx.block = (bytes > DISPLAY_DMA_BLOCK_MAX) ? DISPLAY_DMA_BLOCK_MAX : bytes;
    s_tx.repeat = false;
    s_tx.active = true;
    err = panel_tx_run_();
  }

  os_port_mutex_unlock(&s_lock);
  return err;
}

lcd_err_t panel_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                     lcd_color_t color) {
  if ((w == 0U) || (h == 0U)) {
    return LCD_ERR_PARAM;
  }
  if (!os_port_mutex_lock(&s_lock, PANEL_TX_TIMEOUT_MS)) {
    return LCD_ERR_BUSY;
  }

  lcd_err_t err = panel_set_window(x, y, w, h);
  if (err == LCD_OK) {
    const uint32_t total = (uint32_t)w * (uint32_t)h * DISPLAY_BPP;
    const uint32_t cap = (uint32_t)sizeof(s_fill_buf);
    const uint32_t filled_bytes = (total > cap) ? cap : total;

    for (uint32_t i = 0U; i < filled_bytes; i += 2U) {
      panel_put_u16_(&s_fill_buf[i], (uint16_t)color);
    }

    s_tx.data = s_fill_buf;
    s_tx.total = total;
    s_tx.sent = 0U;
    s_tx.block = filled_bytes;
    s_tx.repeat = true; /* 纯色：每块都取同一缓冲 */
    s_tx.active = true;
    err = panel_tx_run_();
  }

  os_port_mutex_unlock(&s_lock);
  return err;
}

bool panel_is_busy(void) { return s_tx.active; }

lcd_err_t panel_last_error(void) { return s_last_err; }
