/**
 ******************************************************************************
 * @file    lcd_err.c
 * @brief   L1 显示模块统一错误码实现
 ******************************************************************************
 */

#include "display/lcd_err.h"

const char *lcd_err_str(lcd_err_t err) {
  switch (err) {
  case LCD_OK:
    return "LCD_OK";
  case LCD_ERR_PARAM:
    return "LCD_ERR_PARAM";
  case LCD_ERR_BUS:
    return "LCD_ERR_BUS";
  case LCD_ERR_STATE:
    return "LCD_ERR_STATE";
  case LCD_ERR_BUSY:
    return "LCD_ERR_BUSY";
  case LCD_ERR_UNSUPPORTED:
    return "LCD_ERR_UNSUPPORTED";
  default:
    return "LCD_ERR_UNKNOWN";
  }
}
