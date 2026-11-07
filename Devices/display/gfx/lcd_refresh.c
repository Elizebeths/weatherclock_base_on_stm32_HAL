/**
 ******************************************************************************
 * @file    lcd_refresh.c
 * @brief   L1 局部刷新调度器实现
 *
 * @note    条带缓冲是唯一的额外 RAM 开销：
 *          DISPLAY_WIDTH * DISPLAY_STRIP_LINES * DISPLAY_BPP
 *          默认 240 * 2 * 2 = 960 B。
 ******************************************************************************
 */

#include "display/gfx/lcd_refresh.h"

#include <stddef.h>

#include "app_config.h"
#include "common/os_port.h"
#include "display/gfx/lcd_dirty.h"
#include "display/panel/panel_st7789.h"

/** 访问脏区表/刷新状态的互斥等待上限（毫秒） */
#define LCD_REFRESH_LOCK_TIMEOUT_MS 200U

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

/** 条带缓冲：局部刷新时先在 RAM 合成，再整块提交（消除逐段涂写的闪烁） */
static uint8_t s_strip[DISPLAY_WIDTH * DISPLAY_STRIP_LINES * DISPLAY_BPP];

static os_mutex_t s_lock; /**< 保护脏区表 */
static os_sem_t s_req;    /**< 「有脏区」通知（RTOS 模式） */
static os_task_t s_task;  /**< 显示任务（RTOS 模式） */
static lcd_render_cb_t s_render;
static bool s_ready = false;

/* ==========================================================================
 * 内部：脏区 → 条带 → 提交
 * ========================================================================== */

/**
 * @brief 渲染并提交一个脏区
 * @note  按 DISPLAY_STRIP_LINES 行切条带：每条带先在 RAM 合成（调用应用回调），
 *        再一次性 DMA 提交。脏区高度不足一条带时按实际行数提交，不做无谓填充。
 */
static lcd_err_t lcd_refresh_render_area_(const lcd_rect_t *area) {
  const uint16_t step = (uint16_t)DISPLAY_STRIP_LINES;

  for (uint16_t off = 0U; off < area->h; off = (uint16_t)(off + step)) {
    uint16_t lines = (uint16_t)(area->h - off);
    if (lines > step) {
      lines = step;
    }

    const lcd_rect_t strip = lcd_rect_make(area->x, (uint16_t)(area->y + off),
                                           area->w, lines);
    lcd_canvas_t canvas;
    lcd_canvas_buffer(&canvas, s_strip, area->w, &strip);

    /* 应用只负责「把这个区域画出来」，坐标仍是屏幕坐标 */
    s_render(&strip, &canvas);
    if (canvas.err != LCD_OK) {
      return canvas.err;
    }

    const lcd_err_t err = panel_blit(strip.x, strip.y, strip.w, strip.h, s_strip);
    if (err != LCD_OK) {
      return err;
    }
  }
  return LCD_OK;
}

/* ==========================================================================
 * 任务体 / 驱动入口
 * ========================================================================== */

/**
 * @brief 显示任务体（仅 RTOS 模式创建）
 * @note  阻塞等待「有脏区」通知后冲刷；超时醒来是为了兜底处理
 *        「通知丢失」或异常路径，并保持对总线错误的自恢复机会。
 */
static void lcd_refresh_task_entry_(void *arg) {
  (void)arg;
  for (;;) {
    (void)os_port_sem_take(&s_req, APP_DISPLAY_DIRTY_TIMEOUT_MS);
    (void)lcd_refresh_flush();
  }
}

void lcd_refresh_poll(void) {
	if (!s_ready)					return;
	if (os_port_is_rtos())			return; /* 刷新由显示任务负责 */
	(void)lcd_refresh_flush();
}

bool lcd_refresh_is_dirty(void) {
  bool dirty = false;
  if (os_port_mutex_lock(&s_lock, LCD_REFRESH_LOCK_TIMEOUT_MS)) {
    dirty = !lcd_dirty_is_empty();
    os_port_mutex_unlock(&s_lock);
  }
  return dirty;
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */

lcd_err_t lcd_refresh_init(lcd_render_cb_t on_render) {
  if (on_render == NULL) {
    return LCD_ERR_PARAM;
  }

  s_render = on_render;
  os_port_mutex_init(&s_lock);
  os_port_sem_init(&s_req, 0U, 1U);
  lcd_dirty_reset();
  s_ready = true;

  if (os_port_is_rtos()) {
    const bool ok = os_port_task_create(&s_task, lcd_refresh_task_entry_, "lcd",
                                        NULL, APP_DISPLAY_TASK_STACK_WORDS,
                                        APP_DISPLAY_TASK_PRIORITY);
    if (!ok) {
      s_ready = false;
      return LCD_ERR_STATE;
    }
  } else {
    /* 裸机：登记任务体但不创建真实任务，由 lcd_refresh_poll() 驱动 */
    (void)os_port_task_create(&s_task, lcd_refresh_task_entry_, "lcd", NULL,
                              APP_DISPLAY_TASK_STACK_WORDS,
                              APP_DISPLAY_TASK_PRIORITY);
  }
  return LCD_OK;
}

lcd_err_t lcd_invalidate(const lcd_rect_t *r) {
  if (!s_ready) {
    return LCD_ERR_STATE;
  }
  if (lcd_rect_is_empty(r)) {
    return LCD_OK;
  }
  if (!os_port_mutex_lock(&s_lock, LCD_REFRESH_LOCK_TIMEOUT_MS)) {
    return LCD_ERR_BUSY;
  }
  (void)lcd_dirty_add(r);
  os_port_mutex_unlock(&s_lock);

  if (os_port_is_rtos()) {
    os_port_sem_give(&s_req);
  }
  return LCD_OK;
}

lcd_err_t lcd_invalidate_screen(void) {
  if (!s_ready) {
    return LCD_ERR_STATE;
  }
  if (!os_port_mutex_lock(&s_lock, LCD_REFRESH_LOCK_TIMEOUT_MS)) {
    return LCD_ERR_BUSY;
  }
  lcd_dirty_add_screen();
  os_port_mutex_unlock(&s_lock);

  if (os_port_is_rtos()) {
    os_port_sem_give(&s_req);
  }
  return LCD_OK;
}

lcd_err_t lcd_refresh_flush(void) {
  if (!s_ready)		return LCD_ERR_STATE;

  lcd_rect_t areas[DISPLAY_DIRTY_MAX];
  uint32_t count = 0U;

  if (!os_port_mutex_lock(&s_lock, LCD_REFRESH_LOCK_TIMEOUT_MS))	return LCD_ERR_BUSY;
  const bool taken = lcd_dirty_take_all(areas, DISPLAY_DIRTY_MAX, &count);
  os_port_mutex_unlock(&s_lock);

  if (!taken || (count == 0U))		return LCD_OK;

  lcd_err_t err = LCD_OK;
  for (uint32_t i = 0U; i < count; i++) {
    if (lcd_rect_is_empty(&areas[i]))		continue;
    err = lcd_refresh_render_area_(&areas[i]);
    if (err != LCD_OK) {
      break; /* 总线异常：本轮剩余区域放弃，留待修复后重新标脏 */
    }
  }
  return err;
}
