/**
 ******************************************************************************
 * @file    lcd_canvas.c
 * @brief   L1 画布后端实现：直写面板 与 内存条带
 ******************************************************************************
 */

#include "display/gfx/lcd_canvas.h"

#include <stddef.h>

#include "display/panel/panel_st7789.h"

/* ==========================================================================
 * 构造
 * ========================================================================== */

void lcd_canvas_direct(lcd_canvas_t *c, const lcd_rect_t *area) {
  if (c == NULL) {
    return;
  }
  c->buf = NULL;
  c->stride = 0U;
  c->bounds = (area != NULL) ? *area : lcd_rect_screen();
  c->err = LCD_OK;
}

void lcd_canvas_buffer(lcd_canvas_t *c, uint8_t *buf, uint16_t stride,
                       const lcd_rect_t *area) {
  if ((c == NULL) || (buf == NULL) || (area == NULL)) {
    return;
  }
  c->buf = buf;
  c->stride = stride;
  c->bounds = *area;
  c->err = LCD_OK;
}

/* ==========================================================================
 * 裁剪
 * ========================================================================== */

bool lcd_canvas_clip(const lcd_canvas_t *c, const lcd_rect_t *r,
                     lcd_rect_t *out) {
  if (c == NULL) {
    return false;
  }
  return lcd_rect_intersect(&c->bounds, r, out);
}

/* ==========================================================================
 * 后端原语
 * ========================================================================== */

void lcd_canvas_rect(lcd_canvas_t *c, const lcd_rect_t *r, lcd_color_t color) {
  if ((c == NULL) || lcd_rect_is_empty(r)) {
    return;
  }
  if (panel_is_busy()) {
    c->err = LCD_ERR_BUSY;
    return;
  }

  if (c->buf == NULL) {
    /* 直写面板：一次设窗 + 分块 DMA 重复提交纯色 */
    const lcd_err_t err = panel_fill(r->x, r->y, r->w, r->h, color);
    if (err != LCD_OK) {
      c->err = err;
    }
    return;
  }

  /* 内存条带：按行写入。缓冲充足时整块写一行，减少循环开销。 */
  const uint16_t hi = (uint8_t)((uint16_t)color >> 8U);
  const uint8_t lo = (uint8_t)color;
  for (uint16_t row = 0U; row < r->h; row++) {
    const uint32_t y = (uint32_t)(r->y - c->bounds.y) + row;
    uint8_t *p = &c->buf[(y * c->stride + (uint32_t)(r->x - c->bounds.x)) *
                         DISPLAY_BPP];
    for (uint16_t i = 0U; i < r->w; i++) {
      p[0] = hi;
      p[1] = lo;
      p += DISPLAY_BPP;
    }
  }
}

void lcd_canvas_blit(lcd_canvas_t *c, const lcd_rect_t *r, const uint8_t *src,
                     uint16_t src_stride_px) {
  if ((c == NULL) || lcd_rect_is_empty(r) || (src == NULL)) {
    return;
  }

  if (c->buf == NULL) {
    /* 直写面板要求源数据与目标同宽（panel_blit 只接受紧凑数据） */
    if (src_stride_px != r->w) {
      c->err = LCD_ERR_UNSUPPORTED;
      return;
    }
    if (panel_is_busy()) {
      c->err = LCD_ERR_BUSY;
      return;
    }
    const lcd_err_t err = panel_blit(r->x, r->y, r->w, r->h, src);
    if (err != LCD_OK) {
      c->err = err;
    }
    return;
  }

  const uint32_t src_row_bytes = (uint32_t)src_stride_px * DISPLAY_BPP;
  const uint32_t dst_row_bytes = (uint32_t)r->w * DISPLAY_BPP;
  for (uint16_t row = 0U; row < r->h; row++) {
    const uint32_t y = (uint32_t)(r->y - c->bounds.y) + row;
    uint8_t *dst = &c->buf[(y * c->stride + (uint32_t)(r->x - c->bounds.x)) *
                           DISPLAY_BPP];
    const uint8_t *s = &src[(uint32_t)row * src_row_bytes];
    for (uint32_t i = 0U; i < dst_row_bytes; i++) {
      dst[i] = s[i];
    }
  }
}
