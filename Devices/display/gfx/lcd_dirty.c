/**
 ******************************************************************************
 * @file    lcd_dirty.c
 * @brief   L1 脏矩形管理器实现
 ******************************************************************************
 */

#include "display/gfx/lcd_dirty.h"

#include <stddef.h>

/* 脏区表：定长、静态存储（RAM = DISPLAY_DIRTY_MAX * 8 B） */
static lcd_rect_t s_rects[DISPLAY_DIRTY_MAX];
static uint32_t s_count;

/* ==========================================================================
 * 查询
 * ========================================================================== */

void lcd_dirty_reset(void) { s_count = 0U; }

bool lcd_dirty_is_empty(void) { return s_count == 0U; }

uint32_t lcd_dirty_count(void) { return s_count; }

bool lcd_dirty_get(uint32_t index, lcd_rect_t *out) {
  if ((out == NULL) || (index >= s_count)) {
    return false;
  }
  *out = s_rects[index];
  return true;
}

/* ==========================================================================
 * 登记
 * ========================================================================== */

/** @brief 两项并集相对前者的面积增量（用于挑选「最不坏」的合并目标） */
static uint32_t lcd_dirty_growth_(const lcd_rect_t *base,
                                  const lcd_rect_t *add) {
  lcd_rect_t u;
  lcd_rect_union(base, add, &u);
  const uint32_t before = (uint32_t)base->w * base->h;
  const uint32_t after = (uint32_t)u.w * u.h;
  return (after > before) ? (after - before) : 0U;
}

/**
 * @brief 消除表内所有相交项：把相交的两项合并，并移除被吸收的那一项
 * @note  合并可能产生新的相交（A∪B 又碰到 C），因此循环至稳定；
 *        DISPLAY_DIRTY_MAX 很小（默认 8），O(n^2) 的重复扫描代价可忽略。
 */
static void lcd_dirty_compact_(void) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (uint32_t i = 0U; (i < s_count) && !changed; i++) {
      for (uint32_t j = i + 1U; j < s_count; j++) {
        lcd_rect_t dummy;
        if (lcd_rect_intersect(&s_rects[i], &s_rects[j], &dummy)) {
          lcd_rect_union(&s_rects[i], &s_rects[j], &s_rects[i]);
          /* 用末项填补被吸收项，保持表紧凑 */
          s_rects[j] = s_rects[s_count - 1U];
          s_count--;
          changed = true;
          break;
        }
      }
    }
  }
}

bool lcd_dirty_add(const lcd_rect_t *r) {
  if (lcd_rect_is_empty(r))		return false;

  /* 与屏幕求交：屏外部分直接丢弃 */
  const lcd_rect_t screen = lcd_rect_screen();
  lcd_rect_t clip;
  if (!lcd_rect_intersect(&screen, r, &clip))		return true; /* 完全在屏外：视为已处理 */

  /* 能并入某个相交项就直接并入（并顺手消除由此产生的相交项） */
  for (uint32_t i = 0U; i < s_count; i++) {
    lcd_rect_t dummy;
    if (lcd_rect_intersect(&s_rects[i], &clip, &dummy)) {
      lcd_rect_union(&s_rects[i], &clip, &s_rects[i]);
      lcd_dirty_compact_();
      return true;
    }
  }

  if (s_count < DISPLAY_DIRTY_MAX) {
    s_rects[s_count] = clip;
    s_count++;
    return true;
  }

  /* 表满：并入「面积增量最小」的一项，避免直接膨胀为整屏 */
  uint32_t best = 0U;
  uint32_t best_growth = 0xFFFFFFFFU;
  for (uint32_t i = 0U; i < s_count; i++) {
    const uint32_t growth = lcd_dirty_growth_(&s_rects[i], &clip);
    if (growth < best_growth) {
      best_growth = growth;
      best = i;
    }
  }
  lcd_rect_union(&s_rects[best], &clip, &s_rects[best]);
  lcd_dirty_compact_();
  return true;
}

void lcd_dirty_add_screen(void) {
  lcd_dirty_reset();
  s_rects[0] = lcd_rect_screen();
  s_count = 1U;
}

bool lcd_dirty_take_all(lcd_rect_t *out, uint32_t capacity, uint32_t *count) {
  if ((out == NULL) || (count == NULL) || (capacity == 0U)) {
    return false;
  }

  uint32_t n = s_count;
  if (n > capacity) {
    n = capacity;
  }

  for (uint32_t i = 0U; i < n; i++) {
    out[i] = s_rects[i];
  }
  /* 理论上不会走到：capacity 就是表容量。防御性处理——把溢出的项并进末项，
   * 保证「被改动过」这一信息不丢失（宁可多刷，不可漏刷）。 */
  for (uint32_t i = n; i < s_count; i++) {
    lcd_rect_union(&out[n - 1U], &s_rects[i], &out[n - 1U]);
  }

  *count = n;
  s_count = 0U;
  return true;
}
