/**
 ******************************************************************************
 * @file    lcd_text.c
 * @brief   L1 文本层实现
 ******************************************************************************
 */

#include "display/text/lcd_text.h"

#include <stddef.h>

#include "display/gfx/lcd_draw.h"

/** 数值格式化缓冲区：10 位十进制 + 符号 + 小数点 + 少量填充 */
#define LCD_TEXT_FMT_CAP 24U

/* ==========================================================================
 * 样式
 * ========================================================================== */

lcd_text_style_t lcd_text_style_transparent(lcd_color_t fg) {
  const lcd_text_style_t s = {fg, LCD_COLOR_BLACK, true};
  return s;
}

lcd_text_style_t lcd_text_style_opaque(lcd_color_t fg, lcd_color_t bg) {
  const lcd_text_style_t s = {fg, bg, false};
  return s;
}

/* ==========================================================================
 * 字形取模
 * ========================================================================== */

/** @brief 取某行像素位串（已按 row_inverted 处理上下镜像） */
static const uint8_t *lcd_text_row_bits_(const lcd_font_t *f, uint16_t glyph,
                                         uint16_t row) {
  const uint16_t src_row = f->row_inverted ? (uint16_t)(f->h - 1U - row) : row;
  const uint32_t off = (uint32_t)glyph * lcd_font_char_bytes(f) +
                       (uint32_t)src_row * (uint32_t)f->bytes_per_row;
  return &f->data[off];
}

/** @brief 取某行内第 px 列是否点亮（字节内 bit0 为最左像素） */
static bool lcd_text_bit_(const uint8_t *row_bits, uint16_t px) {
  return ((row_bits[px >> 3U] >> (px & 7U)) & 0x01U) != 0U;
}

/* ==========================================================================
 * 字符 / 字符串
 * ========================================================================== */

void lcd_text_char(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                   char ch, const lcd_text_style_t *style) {
  if ((c == NULL) || (f == NULL) || (style == NULL) || (f->w == 0U) ||
      (f->h == 0U)) {
    return;
  }

  /* 字体不支持的字符退化为空格，保证排版宽度不变 */
  if (!lcd_font_supports(f, ch)) {
    ch = ' ';
  }

  /* 不透明样式：先把整个字符格铺成底色，旧内容被完全覆盖（不留残影） */
  if (!style->transparent) {
    lcd_draw_box(c, x, y, f->w, f->h, style->bg);
  }

  const uint8_t glyph = lcd_font_index(f, ch);
  for (uint16_t row = 0U; row < f->h; row++) {
    const uint8_t *const bits = lcd_text_row_bits_(f, glyph, row);
    uint16_t px = 0U;
    while (px < f->w) {
      if (!lcd_text_bit_(bits, px)) {
        px++;
        continue;
      }
      const uint16_t start = px;
      while ((px < f->w) && lcd_text_bit_(bits, px)) {
        px++;
      }
      /* 一个连续前景游程 = 一次水平线段 = 一次内存写入（条带画布） */
      lcd_draw_hline(c, x + (int32_t)start, y + (int32_t)row,
                     (uint16_t)(px - start), style->fg);
    }
  }
}

void lcd_text_string(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                     const char *s, const lcd_text_style_t *style) {
  if ((c == NULL) || (f == NULL) || (s == NULL) || (style == NULL)) {
    return;
  }

  int32_t cx = x;
  while (*s != '\0') {
    lcd_text_char(c, f, cx, y, *s, style);
    cx += (int32_t)f->w;
    s++;
  }
}

uint16_t lcd_text_measure(const lcd_font_t *f, const char *s) {
  if ((f == NULL) || (s == NULL)) {
    return 0U;
  }
  uint32_t n = 0U;
  while (s[n] != '\0') {
    n++;
  }
  const uint32_t w = n * (uint32_t)f->w;
  return (w > 0xFFFFU) ? 0xFFFFU : (uint16_t)w;
}

/* ==========================================================================
 * 数值格式化
 * ========================================================================== */

/**
 * @brief 把无符号数的十进制数字右对齐写进缓冲
 * @param out 输出缓冲
 * @param cap 输出容量
 * @param v   数值
 * @param width 字段宽度（不足则用 pad 填充；为 0 时按实际位数）
 * @param pad 填充字符；'\0' 表示不填充
 * @return 写入的字符数
 */
static uint8_t lcd_text_fmt_digits_(char *out, uint8_t cap, uint32_t v,
                                    uint8_t width, char pad) {
  char rev[10];
  uint8_t n = 0U;
  do {
    rev[n] = (char)('0' + (char)(v % 10U));
    n++;
    v /= 10U;
  } while ((v != 0U) && (n < (uint8_t)sizeof(rev)));

  uint8_t field = (width > n) ? width : n;
  if (field > cap) {
    field = cap;
  }

  uint8_t pos = 0U;
  if ((pad != '\0') && (field > n)) {
    while (pos < (uint8_t)(field - n)) {
      out[pos] = pad;
      pos++;
    }
  }
  while ((pos < field) && (n > 0U)) {
    n--;
    out[pos] = rev[n];
    pos++;
  }
  return pos;
}

/** @brief 计算 int32 的绝对值（避免 INT32_MIN 取负溢出） */
static uint32_t lcd_text_abs32_(int32_t v) {
  return (v < 0) ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
}

void lcd_text_u32(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                  uint32_t value, uint8_t digits, char pad_ch,
                  const lcd_text_style_t *style) {
  if ((c == NULL) || (f == NULL) || (style == NULL)) {
    return;
  }
  char buf[LCD_TEXT_FMT_CAP];
  const uint8_t n = lcd_text_fmt_digits_(buf, (uint8_t)sizeof(buf), value, digits,
                                         pad_ch);
  if (n == 0U) {
    return;
  }
  char str[LCD_TEXT_FMT_CAP + 1U];
  for (uint8_t i = 0U; i < n; i++) {
    str[i] = buf[i];
  }
  str[n] = '\0';
  lcd_text_string(c, f, x, y, str, style);
}

void lcd_text_i32(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                  int32_t value, uint8_t digits, char pad_ch,
                  const lcd_text_style_t *style) {
  if ((c == NULL) || (f == NULL) || (style == NULL)) {
    return;
  }

  const bool negative = (value < 0);
  /* 符号占一位，因此数字可用宽度为 digits - 1 */
  const uint8_t num_width = (negative && (digits > 0U)) ? (uint8_t)(digits - 1U)
                                                        : digits;
  char digits_buf[LCD_TEXT_FMT_CAP];
  const uint8_t n = lcd_text_fmt_digits_(digits_buf, (uint8_t)sizeof(digits_buf),
                                         lcd_text_abs32_(value), num_width,
                                         pad_ch);

  char str[LCD_TEXT_FMT_CAP + 2U];
  uint8_t pos = 0U;
  if (negative) {
    str[pos] = '-';
    pos++;
  }
  for (uint8_t i = 0U; i < n; i++) {
    str[pos] = digits_buf[i];
    pos++;
  }
  str[pos] = '\0';
  lcd_text_string(c, f, x, y, str, style);
}

void lcd_text_fixed(lcd_canvas_t *c, const lcd_font_t *f, int32_t x, int32_t y,
                    int32_t scaled, uint8_t frac_digits, uint8_t digits,
                    char pad_ch, const lcd_text_style_t *style) {
  if ((c == NULL) || (f == NULL) || (style == NULL)) {
    return;
  }
  if (frac_digits > 6U) {
    frac_digits = 6U;
  }

  const bool negative = (scaled < 0);
  char digits_buf[LCD_TEXT_FMT_CAP];
  /* 先按「至少 frac_digits 位（不足补前导 0）」取出有效数字，再插入小数点 */
  const uint8_t n = lcd_text_fmt_digits_(digits_buf, (uint8_t)sizeof(digits_buf),
                                         lcd_text_abs32_(scaled), 0U, '\0');

  const uint8_t npad = (uint8_t)((frac_digits > 0U) ? (frac_digits + 1U) : 0U);
  const uint8_t total = (n > npad) ? n : npad;

  char str[LCD_TEXT_FMT_CAP + 3U];
  uint8_t pos = 0U;
  if (negative) {
    str[pos] = '-';
    pos++;
  }

  /* 字段宽度：整数部分按 digits 补齐（pad_ch），小数部分不动 */
  const uint8_t int_len = (total > frac_digits) ? (uint8_t)(total - frac_digits) : 1U;
  if ((pad_ch != '\0') && (digits > int_len)) {
    uint8_t padn = (uint8_t)(digits - int_len);
    if ((uint8_t)(pos + padn + total + (frac_digits > 0U ? 1U : 0U)) >
        (uint8_t)sizeof(str)) {
      padn = (uint8_t)(sizeof(str) - pos - total - (frac_digits > 0U ? 1U : 0U));
    }
    while (padn > 0U) {
      str[pos] = pad_ch;
      pos++;
      padn--;
    }
  }

  for (uint8_t i = 0U; i < total; i++) {
    /* 有效数字右侧对齐到 total 长度：不足部分补 '0' */
    const uint8_t from_end = (uint8_t)(total - 1U - i);
    const char ch = (from_end < n) ? digits_buf[(uint8_t)(n - 1U - from_end)] : '0';
    /* 小数点位于「整数部分之后」：第 (total - frac_digits) 个字符之前 */
    if ((frac_digits > 0U) && (i == (uint8_t)(total - frac_digits))) {
      str[pos] = '.';
      pos++;
    }
    str[pos] = ch;
    pos++;
  }
  str[pos] = '\0';

  lcd_text_string(c, f, x, y, str, style);
}
