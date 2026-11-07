/**
 ******************************************************************************
 * @file    lcd_dev.c
 * @brief   L1 设备层实现
 ******************************************************************************
 */

#include "display/lcd_dev.h"

#include "display/lcd_port.h"
#include "display/panel/panel_st7789.h"

lcd_err_t lcd_dev_init(void) {
  /* 初始化期间先关背光，避免上电白屏/花屏闪烁（序列结束后由调用方点亮） */
  lcd_bl_off();

  lcd_err_t err = panel_init();
  if (err != LCD_OK) {
    return err;
  }

  /* 复位时序 + 寄存器序列 + 打开显示（全部由驱动层的序列表驱动） */
  err = panel_start();
  if ((err == LCD_OK) && lcd_is_bus_error()) {
    err = LCD_ERR_BUS;
  }

  /* 面板复位后显存内容是**随机的**：必须铺一层已知底色，否则任何「没被画到」
   * 的区域都会显示随机显存内容，即肉眼可见的「花屏背景」。
   * 这一步与旧实现 LCD_init() 末尾的清屏完全等价，属于设备上电的确定性前提，
   * 不能依赖调用方是否记得自行清屏（自检图案就只绘制局部元素）。
   * 用 panel_fill 直接提交纯色块：一次设窗 + 分块 DMA，开销远小于逐行轮询。 */
  if (err == LCD_OK) {
    err = panel_fill(0U, 0U, (uint16_t)LCD_WIDTH, (uint16_t)LCD_HEIGHT,
                     LCD_COLOR_BLACK);
  }
  return err;
}

void lcd_bl_on(void) { lcd_port_pin_write(LCD_PIN_BL, true); }

void lcd_bl_off(void) { lcd_port_pin_write(LCD_PIN_BL, false); }

bool lcd_is_bus_error(void) { return lcd_port_is_bus_error(); }

void lcd_dev_clear_bus_error(void) { lcd_port_clear_bus_error(); }
