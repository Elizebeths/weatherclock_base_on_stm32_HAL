/**
 ******************************************************************************
 * @file    lcd_font.c
 * @brief   L1 内置字体描述符定义
 ******************************************************************************
 */

#include "display/text/lcd_font.h"

#include "display/text/lcd_font_ascii16x32.h"

const lcd_font_t LCD_FONT_ASCII16X32 = {
    .w = LCD_FONT_ASCII16X32_W,
    .h = LCD_FONT_ASCII16X32_H,
    .first = (uint8_t)LCD_FONT_ASCII16X32_FIRST,
    .last = (uint8_t)LCD_FONT_ASCII16X32_LAST,
    .bytes_per_row = LCD_FONT_ASCII16X32_BYTES_PER_ROW,
    .row_inverted = true, /* 源库字模为上下镜像，配套 MY=1 */
    .data = &ascii_1632[0][0],
};
