/**
 ******************************************************************************
 * @file    lcd_port_stm32.c
 * @brief   L0 显示硬件抽象 —— STM32F1 实现
 *
 * @note    SPI 字节搬运委托给 Ports/bus/spi_port（一个句柄、一把总线锁、一处
 *          DMA 通道），本文件只负责：显示语义（CS/DC/RST/BL）、CS 时序、
 *          提交/完成回调、总线错误门闩。
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：lcd.c 的引脚宏（第 29-36 行）、LCD_BL_on/off（38-44）、
 *                    LCD_transfer_ 的超时与错误门闩（63-76）、
 *                    LCD_writeDATStream_ 的 CS 全程拉低策略（112-129）
 *   一级 · 大型开源：Bodmer/TFT_eSPI 的 DMA 时序策略 —— CS 在整块传输期间
 *                    保持拉低、传输完成回调里才释放
 *   一级 · 大型开源：LVGL port 层「提交/完成」分离 —— 本层只搬一块，
 *                    双缓冲与下一块策略留在 L1 的 lcd_panel
 *   三级 · 小型开源：未使用
 *
 * CS 语义（重要）：CS 由本层在每次提交时拉低、在完成回调里拉高，形成严格配对。
 * 需要连续多块写入时，L1 应在完成回调中提交下一块，而不是自己操作 CS。
 * DC 不属于 CS 语义，仍由 L1 在提交前用 lcd_port_pin_write() 设置。
 ******************************************************************************
 */

#include "lcd_port.h"

#include <stddef.h> /* NULL */

#include "board.h"
#include "bus/spi_port.h"
#include "gpio/gpio_port.h"
/* 注：本文件刻意不包含 main.h —— 它在 L0 内部把 SPI 与引脚都委托给了
 * spi_port / gpio_port，因此自身不出现任何 HAL 符号。这是「L0 也保持
 * 职责单一」的一个副产品：只有真正碰到外设寄存器的那一层才含 HAL。 */

/* ==========================================================================
 * 私有数据
 * ========================================================================== */

/** 显示引脚映射表：把 lcd_port_pin_t 翻译为 board.h 的中性引脚 */
static const uint8_t s_pin_bank[LCD_PIN_MAX] = {
    BOARD_TFT_CS_BANK, BOARD_TFT_DC_BANK, BOARD_TFT_RST_BANK,
    BOARD_TFT_BL_BANK};
static const uint8_t s_pin_index[LCD_PIN_MAX] = {
    BOARD_TFT_CS_PIN_INDEX, BOARD_TFT_DC_PIN_INDEX, BOARD_TFT_RST_PIN_INDEX,
    BOARD_TFT_BL_PIN_INDEX};

/** 总线错误门闩：一旦传输失败即置位，后续 SPI 调用立即返回（与旧驱动等效） */
static bool s_bus_error = false;

/** DMA 提交状态 */
static bool s_dma_pending = false;
static lcd_port_done_cb_t s_user_cb = NULL;
static void *s_user_arg = NULL;

static gpio_pin_t pin_of(lcd_port_pin_t pin) {
  return BOARD_PIN_OF(s_pin_bank[pin], s_pin_index[pin]);
}

/* ==========================================================================
 * 一、生命周期
 * ========================================================================== */

port_status_t lcd_port_init(void) {
  /* 安全电平：CS 不选中、DC 默认数据、RST 不复位、背光灭
   * （内部使用非极性语义的 gpio 直写，避免 BL 极性在此处二次取反） */
  gpio_port_write(pin_of(LCD_PIN_CS), true);
  gpio_port_write(pin_of(LCD_PIN_DC), true);
  gpio_port_write(pin_of(LCD_PIN_RST), true);
  lcd_port_pin_write(LCD_PIN_BL, false);

  s_bus_error = false;
  s_dma_pending = false;
  s_user_cb = NULL;
  s_user_arg = NULL;

  /* spi_port_init() 幂等：若 CubeMX 已调用 MX_SPI1_Init()，此处仅做就绪检查 */
  return spi_port_init();
}

/* ==========================================================================
 * 二、引脚 / 延时 / 计时 / 临界区
 * ========================================================================== */

void lcd_port_pin_write(lcd_port_pin_t pin, bool level) {
  if ((uint32_t)pin >= (uint32_t)LCD_PIN_MAX) {
    return;
  }
  if (pin == LCD_PIN_BL) {
    /* 背光对外语义统一为「true = 点亮」，物理极性在此处收敛 */
#if BOARD_TFT_BL_ACTIVE_HIGH
    gpio_port_write(pin_of(pin), level);
#else
    gpio_port_write(pin_of(pin), !level);
#endif
    return;
  }
  gpio_port_write(pin_of(pin), level);
}

void lcd_port_delay_ms(uint32_t ms) { port_delay_ms(ms); }

void lcd_port_enter_critical(void) { port_enter_critical(); }

void lcd_port_exit_critical(void) { port_exit_critical(); }

uint32_t lcd_port_cycles(void) { return port_cycles(); }

/* ==========================================================================
 * 三、SPI 传输 —— 轮询
 * ========================================================================== */

port_status_t lcd_port_spi_tx(const uint8_t *data, uint16_t len) {
  if (s_bus_error) {
    return PORT_ERROR; /* 门闩已置位：不再发起，避免每块都等一次超时 */
  }
  if (data == NULL) {
    return PORT_ERROR;
  }
  if (len == 0U) {
    return PORT_OK;
  }

  spi_port_lock();
  lcd_port_pin_write(LCD_PIN_CS, false);
  const port_status_t st = spi_port_tx(data, len, APP_SPI_LCD_TIMEOUT_MS);
  lcd_port_pin_write(LCD_PIN_CS, true);
  spi_port_unlock();

  if (st != PORT_OK) {
    s_bus_error = true;
  }
  return st;
}

port_status_t lcd_port_cmd_dat(uint8_t cmd, const uint8_t *dat, uint16_t len) {
  if (s_bus_error) {
    return PORT_ERROR;
  }

  /* 命令与参数共享一次 CS 会话：CS 全程拉低，二者之间只切换 DC */
  spi_port_lock();
  lcd_port_pin_write(LCD_PIN_CS, false);
  lcd_port_pin_write(LCD_PIN_DC, false);
  port_status_t st = spi_port_tx(&cmd, 1U, APP_SPI_LCD_TIMEOUT_MS);
  if ((st == PORT_OK) && (dat != NULL) && (len > 0U)) {
    lcd_port_pin_write(LCD_PIN_DC, true);
    st = spi_port_tx(dat, len, APP_SPI_LCD_TIMEOUT_MS);
  }
  lcd_port_pin_write(LCD_PIN_DC, true); /* 恢复数据态，与旧驱动一致 */
  lcd_port_pin_write(LCD_PIN_CS, true);
  spi_port_unlock();

  if (st != PORT_OK) {
    s_bus_error = true;
  }
  return st;
}

/* ==========================================================================
 * 四、SPI 传输 —— DMA 单块提交
 * ========================================================================== */

/** @brief DMA 完成内部回调：先释放 CS，再解除总线锁，最后在锁外通知上层 */
static void lcd_dma_done(port_status_t status, void *arg) {
  (void)arg;

  s_dma_pending = false;
  lcd_port_pin_write(LCD_PIN_CS, true); /* TxCplt 之后才拉高 CS */
  if (status != PORT_OK) {
    s_bus_error = true;
  }

  spi_port_unlock(); /* 释放本块持有的总线（与提交时的 lock 配对） */

  /* 在锁外回调：允许上层立即提交下一块（双缓冲切换） */
  const lcd_port_done_cb_t cb = s_user_cb;
  void *const uarg = s_user_arg;
  s_user_cb = NULL;
  s_user_arg = NULL;
  if (cb != NULL) {
    cb(status, uarg);
  }
}

port_status_t lcd_port_spi_tx_dma(const uint8_t *data, uint32_t len,
                                  lcd_port_done_cb_t cb, void *arg) {
  if (s_bus_error) {
    return PORT_ERROR;
  }
  if ((data == NULL) || (len == 0U)) {
    return PORT_ERROR;
  }
  if (len > APP_SPI_LCD_DMA_MAX_BYTES) {
    return PORT_ERROR; /* HAL_SPI_Transmit_DMA 的 Size 为 uint16_t */
  }
  if (s_dma_pending) {
    return PORT_BUSY;
  }

  spi_port_lock();
  lcd_port_pin_write(LCD_PIN_CS, false);

  s_user_cb = cb;
  s_user_arg = arg;
  s_dma_pending = true;

  const port_status_t st = spi_port_tx_dma(data, len, lcd_dma_done, NULL);

  if (st != PORT_OK) {
    /* 提交失败：还原状态与 CS，并补回一次 unlock */
    s_dma_pending = false;
    s_user_cb = NULL;
    s_user_arg = NULL;
    lcd_port_pin_write(LCD_PIN_CS, true);
    s_bus_error = true;
    spi_port_unlock();
    return st;
  }

  /* 若 spi_port 因 DMA 不可用而回退轮询，lcd_dma_done 已同步执行并完成
   * unlock，此时 s_dma_pending 已为 false，此处无需（也不可）再次 unlock。 */
  return PORT_OK;
}

bool lcd_port_spi_busy(void) { return s_dma_pending || spi_port_busy(); }

void lcd_port_spi_abort(void) {
  if (!s_dma_pending) {
    return;
  }
  (void)spi_port_abort();
  s_dma_pending = false;
  /* 中止是调用方主动放弃该块，因此不再回调上层 */
  s_user_cb = NULL;
  s_user_arg = NULL;
  lcd_port_pin_write(LCD_PIN_CS, true);
  spi_port_unlock();
}

/* ==========================================================================
 * 五、总线错误门闩
 * ========================================================================== */

bool lcd_port_is_bus_error(void) { return s_bus_error; }

void lcd_port_clear_bus_error(void) { s_bus_error = false; }
