/**
 ******************************************************************************
 * @file    lcd_font.h
 * @brief   L1 文本层：**字体描述符**（把「字模几何」从全局网格常量解耦）
 *
 * @note    分层位置：L1（Devices/display/text）。不含 HAL / STM32 符号。
 *
 * 为什么要描述符：
 *   旧实现把字模尺寸提升为全局网格常量（LCD_FONT_W/H → LCD_COL_MAX/LINE_MAX），
 *   并用 _Static_assert 强制屏幕尺寸能被字模整除——于是「文本只能落在网格上」，
 *   既不能任意像素定位，也无法混排字号。
 *   现在字模只是一个 lcd_font_t 描述符，文本定位改用像素坐标；
 *   新增字体只需提供一份数据 + 一个描述符，无需改动任何绘制代码。
 ******************************************************************************
 */

#ifndef LCD_FONT_H
#define LCD_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief 点阵字体描述符 */
typedef struct {
  uint16_t w;            /**< 字模宽（像素） */
  uint16_t h;            /**< 字模高（像素） */
  uint8_t first;         /**< 首个可显示字符（含） */
  uint8_t last;          /**< 末个可显示字符（含） */
  uint16_t bytes_per_row;/**< 每行字节数 = ceil(w / 8) */
  bool row_inverted;     /**< 字模是否按行上下镜像存放（配套面板 MY=1 的历史数据） */
  const uint8_t *data;   /**< 字模数据：字符优先，行优先，字节内 bit0 为最左像素 */
} lcd_font_t;

/** @brief 内置 16x32 ASCII 字体（0x20~0x7E） */
extern const lcd_font_t LCD_FONT_ASCII16X32;

/** @brief 每字符占用字节数 */
static inline uint32_t lcd_font_char_bytes(const lcd_font_t *f) {
  return (f == NULL) ? 0U : ((uint32_t)f->bytes_per_row * (uint32_t)f->h);
}

/** @brief 字符在字模数据中的下标（0 基，映射到 [first,last]） */
static inline uint8_t lcd_font_index(const lcd_font_t *f, char ch) {
  return (uint8_t)((uint8_t)ch - f->first);
}

/** @brief 字体是否包含该字符 */
static inline bool lcd_font_supports(const lcd_font_t *f, char ch) {
  const uint8_t u = (uint8_t)ch;
  return (f != NULL) && (u >= f->first) && (u <= f->last);
}

#ifdef __cplusplus
}
#endif

#endif /* LCD_FONT_H */
