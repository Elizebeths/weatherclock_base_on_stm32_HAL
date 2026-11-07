/**
 ******************************************************************************
 * @file    lcd_types.c
 * @brief   L1 矩形运算实现（交集 / 并集）
 ******************************************************************************
 */

#include "display/lcd_types.h"

bool lcd_rect_intersect(const lcd_rect_t *a, const lcd_rect_t *b,
                        lcd_rect_t *out) {
  if (lcd_rect_is_empty(a) || lcd_rect_is_empty(b)) {
    return false;
  }

  const uint16_t x0 = (a->x > b->x) ? a->x : b->x;
  const uint16_t y0 = (a->y > b->y) ? a->y : b->y;
  const uint32_t ax1 = (uint32_t)a->x + a->w;
  const uint32_t ay1 = (uint32_t)a->y + a->h;
  const uint32_t bx1 = (uint32_t)b->x + b->w;
  const uint32_t by1 = (uint32_t)b->y + b->h;
  const uint32_t x1 = (ax1 < bx1) ? ax1 : bx1;
  const uint32_t y1 = (ay1 < by1) ? ay1 : by1;

  if ((x1 <= x0) || (y1 <= y0)) {
    return false;
  }

  if (out != NULL) {
    out->x = x0;
    out->y = y0;
    out->w = (uint16_t)(x1 - x0);
    out->h = (uint16_t)(y1 - y0);
  }
  return true;
}

void lcd_rect_union(const lcd_rect_t *a, const lcd_rect_t *b, lcd_rect_t *out) {
  if (out == NULL) {
    return;
  }
  if (lcd_rect_is_empty(a)) {
    *out = (b != NULL) ? *b : lcd_rect_make(0U, 0U, 0U, 0U);
    return;
  }
  if (lcd_rect_is_empty(b)) {
    *out = *a;
    return;
  }

  const uint16_t x0 = (a->x < b->x) ? a->x : b->x;
  const uint16_t y0 = (a->y < b->y) ? a->y : b->y;
  const uint32_t ax1 = (uint32_t)a->x + a->w;
  const uint32_t ay1 = (uint32_t)a->y + a->h;
  const uint32_t bx1 = (uint32_t)b->x + b->w;
  const uint32_t by1 = (uint32_t)b->y + b->h;
  const uint32_t x1 = (ax1 > bx1) ? ax1 : bx1;
  const uint32_t y1 = (ay1 > by1) ? ay1 : by1;

  out->x = x0;
  out->y = y0;
  out->w = (uint16_t)(x1 - x0);
  out->h = (uint16_t)(y1 - y0);
}
