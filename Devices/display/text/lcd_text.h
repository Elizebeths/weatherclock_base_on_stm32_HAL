/**
 ******************************************************************************
 * @file    lcd_text.h
 * @brief   L1 文本层：**把字符/字符串/数值渲染到画布**（任意像素定位）
 *
 * @note    分层位置：L1（Devices/display/text）。依赖画布（lcd_canvas）、
 *          绘图原语（lcd_draw）与字体描述符（lcd_font）；无 HAL / STM32 符号。
 *
 * 与旧实现的根本差别：
 *   · **坐标自由**：文本按像素坐标定位（int32），不再受「行列网格」限制；
 *     网格如果业务仍需要，由应用层自行用像素换算表达，库不再强加。
 *   · **只有一套 API**：旧 lcd_text 同时提供「透明版」与「*_bg 版」两套接口，
 *     文档还把透明版标注为性能陷阱。现在统一为 lcd_text_style_t：
 *     透明 = 只画前景像素；不透明 = 先铺格底色再画前景。
 *   · **不再逐段设窗**：文本写入画布（通常是内存条带），总线提交由刷新
 *     调度器按条带整块完成，因此不存在「一个字符上百次 SPI 事务」的路径。
 *   · **无浮点依赖**：数值渲染提供 uint32 / int32 / 定点数三种，
 *     定点数用「缩放整数 + 小数位数」表达，彻底去掉 float 与浮点库。
 ******************************************************************************
 */

#ifndef LCD_TEXT_H
#define LCD_TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "display/gfx/lcd_canvas.h"
#include "display/lcd_types.h"
#include "display/text/lcd_font.h"

/** @brief 文本样式 */
typedef struct {
  lcd_color_t fg;    /**< 前景色 */
  lcd_color_t bg;    /**< 格底色（transparent == true 时忽略） */
  bool transparent;  /**< true = 只画前景像素，保留画布原有内容 */
} lcd_text_style_t;

/** @brief 构造「透明」样式（叠加在图形/位图之上时使用） */
lcd_text_style_t lcd_text_style_transparent(lcd_color_t fg);

/** @brief 构造「不透明」样式（先铺底色再画字，UI 常规文本使用） */
lcd_text_style_t lcd_text_style_opaque(lcd_color_t fg, lcd_color_t bg);

/** @brief 单个字符的像素宽度（即字模宽度） */
static inline uint16_t lcd_text_glyph_w(const lcd_font_t *f) {
  return (f == NULL) ? 0U : f->w;
}

/** @brief 单个字符的像素高度（即字模高度） */
static inline uint16_t lcd_text_glyph_h(const lcd_font_t *f) {
  return (f == NULL) ? 0U : f->h;
}

/**
 * @brief 字符串渲染后的像素宽度
 * @note  不含换行处理：'\n' 及其后内容按不可显示字符退化为空格宽度。
 */
uint16_t lcd_text_measure(const lcd_font_t *f, const char *s);

/**
 * @brief 绘制单个字符
 * @param c,f   画布与字体（均不可为 NULL）
 * @param x,y   字符左上角（像素坐标，允许越界，自动裁剪）
 * @param ch    字符；字体不支持的字符退化为空格
 * @param style 样式（不可为 NULL）
 */
void lcd_text_char(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                   char ch, const lcd_text_style_t *style);

/**
 * @brief 绘制字符串（自左向右逐个字符推进）
 * @note  与旧实现不同：**不做「值变短后残留」的处理**——不透明样式下每个
 *        字符格自带底色，超出新串长度的旧格子需要调用方显式填充或标脏清除。
 *        约定：UI 中「值会变短」的字段请在绘制前用 lcd_draw_box 铺一次底色，
 *        或使用本文件的数值接口（它们按字段宽度完整覆盖）。
 */
void lcd_text_string(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                     const char *s, const lcd_text_style_t *style);

/**
 * @brief 绘制无符号整数（右对齐到 digits 个字模宽度的字段内）
 * @param digits 字段宽度（字符数）；实际位数超出时不截断
 * @param pad_ch 填充字符；'\0' 表示「不填充」（只画有效位）
 */
void lcd_text_u32(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                  uint32_t value, uint8_t digits, char pad_ch,
                  const lcd_text_style_t *style);

/**
 * @brief 绘制有符号整数（负号占一位，计入字段宽度）
 * @param digits 字段宽度（字符数）
 */
void lcd_text_i32(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                  int32_t value, uint8_t digits, char pad_ch,
                  const lcd_text_style_t *style);

/**
 * @brief 绘制定点小数（**无浮点依赖**）
 * @param scaled      缩放后的整数，例如 26.5 且 frac_digits=3 → 26500
 * @param frac_digits 小数位数（0~6）；小数点额外占一位，不计入 digits
 * @param digits      整数部分的字段宽度
 * @param pad_ch      填充字符；'\0' 表示不填充
 * @note  取代旧 lcd_text_draw_float（后者依赖 float 与浮点运算库）。
 */
void lcd_text_fixed(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                    int32_t scaled, uint8_t frac_digits, uint8_t digits,
                    char pad_ch, const lcd_text_style_t *style);

#ifdef __cplusplus
}
#endif

#endif /* LCD_TEXT_H */
