/**
 ******************************************************************************
 * @file    lcd_draw.c
 * @brief   L1 统一绘图 API 实现
 ******************************************************************************
 */

#include "display/gfx/lcd_draw.h"

#include <stddef.h>

/* ==========================================================================
 * 裁剪
 * ========================================================================== */

/**
 * @brief 把 [x,x+w) x [y,y+h) 裁剪到「画布有效区 ∩ 屏幕」
 * @param out 裁剪结果（屏幕坐标，uint16）
 * @retval true  有可见部分
 */
static bool lcd_draw_clip_box_(const lcd_canvas_t *c, int32_t x, int32_t y,
                               int32_t w, int32_t h, lcd_rect_t *out) {
  if ((c == NULL) || (w <= 0) || (h <= 0)) {
    return false;
  }

  int32_t x0 = (x > 0) ? x : 0;
  int32_t y0 = (y > 0) ? y : 0;
  int32_t x1 = x + w;
  int32_t y1 = y + h;
  if (x1 > (int32_t)LCD_WIDTH) {
    x1 = (int32_t)LCD_WIDTH;
  }
  if (y1 > (int32_t)LCD_HEIGHT) {
    y1 = (int32_t)LCD_HEIGHT;
  }

  if (x0 < (int32_t)c->bounds.x) {
    x0 = (int32_t)c->bounds.x;
  }
  if (y0 < (int32_t)c->bounds.y) {
    y0 = (int32_t)c->bounds.y;
  }
  if (x1 > (int32_t)c->bounds.x + (int32_t)c->bounds.w) {
    x1 = (int32_t)c->bounds.x + (int32_t)c->bounds.w;
  }
  if (y1 > (int32_t)c->bounds.y + (int32_t)c->bounds.h) {
    y1 = (int32_t)c->bounds.y + (int32_t)c->bounds.h;
  }

  if ((x1 <= x0) || (y1 <= y0)) {
    return false;
  }

  if (out != NULL) {
    out->x = (uint16_t)x0;
    out->y = (uint16_t)y0;
    out->w = (uint16_t)(x1 - x0);
    out->h = (uint16_t)(y1 - y0);
  }
  return true;
}

/** @brief 整数平方根（向下取整） */
static uint32_t lcd_draw_isqrt_(uint32_t n) {
  uint32_t res = 0U;
  uint32_t bit = 1UL << 30U;
  while (bit > n) {
    bit >>= 2U;
  }
  while (bit != 0U) {
    if (n >= res + bit) {
      n -= res + bit;
      res = (res >> 1U) + bit;
    } else {
      res >>= 1U;
    }
    bit >>= 2U;
  }
  return res;
}

/* ==========================================================================
 * 基础原语
 * ========================================================================== */

void lcd_draw_clear(lcd_canvas_t *c, lcd_color_t color) {
  if (c == NULL) {
    return;
  }
  lcd_canvas_rect(c, &c->bounds, color);
}

void lcd_draw_hline(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t w,
                    lcd_color_t color) {
  lcd_rect_t r;
  if (lcd_draw_clip_box_(c, x, y, (int32_t)w, 1, &r)) {
    lcd_canvas_rect(c, &r, color);
  }
}

void lcd_draw_vline(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t h,
                    lcd_color_t color) {
  lcd_rect_t r;
  if (lcd_draw_clip_box_(c, x, y, 1, (int32_t)h, &r)) {
    lcd_canvas_rect(c, &r, color);
  }
}

void lcd_draw_pixel(lcd_canvas_t *c, int32_t x, int32_t y, lcd_color_t color) {
  lcd_draw_hline(c, x, y, 1U, color);
}

void lcd_draw_box(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t w, uint16_t h,
                  lcd_color_t color) {
  lcd_rect_t out;
  if (lcd_draw_clip_box_(c, x, y, (int32_t)w, (int32_t)h, &out)) {
    lcd_canvas_rect(c, &out, color);
  }
}

void lcd_fill_rect(lcd_canvas_t *c, const lcd_rect_t *r, lcd_color_t color) {
  if (lcd_rect_is_empty(r)) {
    return;
  }
  lcd_draw_box(c, (int32_t)r->x, (int32_t)r->y, r->w, r->h, color);
}

void lcd_draw_rect_outline(lcd_canvas_t *c, const lcd_rect_t *r,
                           lcd_color_t color) {
  if (lcd_rect_is_empty(r)) {
    return;
  }
  lcd_draw_hline(c, (int32_t)r->x, (int32_t)r->y, r->w, color);
  lcd_draw_hline(c, (int32_t)r->x, (int32_t)(r->y + r->h - 1U), r->w, color);
  lcd_draw_vline(c, (int32_t)r->x, (int32_t)r->y, r->h, color);
  lcd_draw_vline(c, (int32_t)(r->x + r->w - 1U), (int32_t)r->y, r->h, color);
}

/* ==========================================================================
 * 直线：按主轴分组为游程，批量提交
 * ========================================================================== */

/** @brief x 主轴：逐列推进，遇到 y 变化即把上一段作为水平游程提交 */
static void lcd_draw_line_x_major_(lcd_canvas_t *c, int32_t x0, int32_t y0,
                                   int32_t x1, int32_t y1, lcd_color_t color) {
  const int32_t dx = x1 - x0;
  const int32_t ady = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
  const int32_t sy = (y1 >= y0) ? 1 : -1;

  int32_t y = y0;
  int32_t num = 0;
  int32_t run_x = x0;
  int32_t run_len = 0;
  int32_t run_y = y0;

  for (int32_t x = x0; x <= x1; x++) {
    if ((run_len > 0) && (y != run_y)) {
      lcd_draw_hline(c, run_x, run_y, (uint16_t)run_len, color);
      run_len = 0;
    }
    if (run_len == 0) {
      run_x = x;
      run_y = y;
    }
    run_len++;

    if (dx > 0) {
      num += ady;
      if (num >= dx) {
        num -= dx;
        y += sy;
      }
    }
  }
  if (run_len > 0) {
    lcd_draw_hline(c, run_x, run_y, (uint16_t)run_len, color);
  }
}

/** @brief y 主轴：逐行推进，遇到 x 变化即把上一段作为垂直游程提交 */
static void lcd_draw_line_y_major_(lcd_canvas_t *c, int32_t x0, int32_t y0,
                                   int32_t x1, int32_t y1, lcd_color_t color) {
  const int32_t dy = y1 - y0;
  const int32_t adx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
  const int32_t sx = (x1 >= x0) ? 1 : -1;

  int32_t x = x0;
  int32_t num = 0;
  int32_t run_y = y0;
  int32_t run_len = 0;
  int32_t run_x = x0;

  for (int32_t y = y0; y <= y1; y++) {
    if ((run_len > 0) && (x != run_x)) {
      lcd_draw_vline(c, run_x, run_y, (uint16_t)run_len, color);
      run_len = 0;
    }
    if (run_len == 0) {
      run_x = x;
      run_y = y;
    }
    run_len++;

    if (dy > 0) {
      num += adx;
      if (num >= dy) {
        num -= dy;
        x += sx;
      }
    }
  }
  if (run_len > 0) {
    lcd_draw_vline(c, run_x, run_y, (uint16_t)run_len, color);
  }
}

void lcd_draw_line(lcd_canvas_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   lcd_color_t color) {
  int32_t ax0 = x0;
  int32_t ay0 = y0;
  int32_t ax1 = x1;
  int32_t ay1 = y1;
  int32_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
  int32_t dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);

  if (dx >= dy) {
    /* 归一化为 x 递增，使游程总是从左到右 */
    if (ax0 > ax1) {
      const int32_t tx = ax0;
      ax0 = ax1;
      ax1 = tx;
      const int32_t ty = ay0;
      ay0 = ay1;
      ay1 = ty;
    }
    lcd_draw_line_x_major_(c, ax0, ay0, ax1, ay1, color);
  } else {
    /* 归一化为 y 递增，使游程总是从上到下 */
    if (ay0 > ay1) {
      const int32_t tx = ax0;
      ax0 = ax1;
      ax1 = tx;
      const int32_t ty = ay0;
      ay0 = ay1;
      ay1 = ty;
    }
    lcd_draw_line_y_major_(c, ax0, ay0, ax1, ay1, color);
  }
}

/* ==========================================================================
 * 圆 / 位图
 * ========================================================================== */

void lcd_draw_circle_filled(lcd_canvas_t *c, int32_t cx, int32_t cy,
                            uint16_t radius, lcd_color_t color) {
  if (radius == 0U) {
    lcd_draw_pixel(c, cx, cy, color);
    return;
  }

  const int32_t r = (int32_t)radius;
  const int32_t rr = r * r;
  for (int32_t dy = -r; dy <= r; dy++) {
    const uint32_t half =
        lcd_draw_isqrt_((uint32_t)(rr - (dy * dy)));
    lcd_draw_hline(c, cx - (int32_t)half, cy + dy, (uint16_t)(2U * half + 1U),
                   color);
  }
}

void lcd_draw_bitmap(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t w, uint16_t h,
                     const uint8_t *rgb565) {
  if ((rgb565 == NULL) || (w == 0U) || (h == 0U)) {
    return;
  }

  lcd_rect_t vis;
  if (!lcd_draw_clip_box_(c, x, y, (int32_t)w, (int32_t)h, &vis)) {
    return;
  }

  /* 按裁剪偏移定位源数据：源行长为 w 像素 */
  const uint32_t off = ((uint32_t)(vis.y - (uint16_t)y) * (uint32_t)w +
                        (uint32_t)(vis.x - (uint16_t)x)) *
                       DISPLAY_BPP;
  lcd_canvas_blit(c, &vis, &rgb565[off], w);
}
