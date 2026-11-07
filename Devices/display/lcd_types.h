/**
 ******************************************************************************
 * @file    lcd_types.h
 * @brief   L1 显示模块公共类型：颜色、几何矩形与矩形运算
 *
 * @note    分层位置：L1（Devices/display）。仅依赖 Config/display_cfg.h 与
 *          标准库；**不得出现任何 HAL / STM32 符号**，也**不再包含 board.h**
 *          ——旧链路 lcd_types.h → board.h → gpio/gpio_port.h 会把引脚层
 *          拖进显示类型层，现已被切断（几何的唯一事实来源是 display_cfg.h）。
 *
 * 坐标语义（全模块统一，取代旧驱动「drawScreen 端点不含 / drawLine 端点含」
 * 的历史歧义）：
 *   · 屏幕坐标原点在**左上角**，x 向右、y 向下；
 *   · lcd_rect_t 的 (x,y) 为左上角（含），w/h 为宽高（像素，>0）；
 *   · 所有绘制接口都做裁剪，越界不产生越界访问，也不写出屏外。
 ******************************************************************************
 */

#ifndef LCD_TYPES_H
#define LCD_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "display_cfg.h"
#include "display/lcd_err.h"

/* ==========================================================================
 * 一、几何
 * ========================================================================== */

#define LCD_WIDTH  DISPLAY_WIDTH  /**< 可见区宽度（像素） */
#define LCD_HEIGHT DISPLAY_HEIGHT /**< 可见区高度（像素） */

/** @brief 像素点（左上角为原点） */
typedef struct {
  uint16_t x;
  uint16_t y;
} lcd_point_t;

/**
 * @brief 矩形区域
 * @note  统一语义：x,y 为左上角（含），w,h 为宽高（像素）。
 *        w 或 h 为 0 视为空矩形。
 */
typedef struct {
  uint16_t x;
  uint16_t y;
  uint16_t w;
  uint16_t h;
} lcd_rect_t;

/* ==========================================================================
 * 二、颜色（RGB565）
 * ========================================================================== */

/** @brief RGB565 颜色值（高 5 位 R、中 6 位 G、低 5 位 B） */
typedef uint16_t lcd_color_t;

/** 通过 8-8-8 分量构造 RGB565，便于业务按直觉书写颜色 */
#define LCD_RGB565(r8, g8, b8)                                                 \
  ((lcd_color_t)((((uint16_t)(r8) & 0xF8U) << 8U) |                            \
                 (((uint16_t)(g8) & 0xFCU) << 3U) |                            \
                 (((uint16_t)(b8) & 0xF8U) >> 3U)))

#define LCD_COLOR_BLACK  0x0000U /**< 黑 */
#define LCD_COLOR_MILK   0xFFFFU /**< 白 */
#define LCD_COLOR_AZURE  0x5A9CU /**< 蓝 */
#define LCD_COLOR_PURPLE 0xF81FU /**< 紫 */
#define LCD_COLOR_BLOOD  0xF800U /**< 红 */
#define LCD_COLOR_GREEN  0x0400U /**< 绿 */
#define LCD_COLOR_YELLOW 0xFFE0U /**< 黄 */
#define LCD_COLOR_AMBER  0xBC40U /**< 琥珀 */
#define LCD_COLOR_GRAY   0x8410U /**< 中灰 */

/* ==========================================================================
 * 三、矩形运算（纯函数，无副作用）
 * ========================================================================== */

/** @brief 空矩形（宽或高为 0） */
static inline bool lcd_rect_is_empty(const lcd_rect_t *r) {
  return (r == NULL) || (r->w == 0U) || (r->h == 0U);
}

/** @brief 矩形是否完全落在屏幕内（宽高非零） */
static inline bool lcd_rect_in_screen(const lcd_rect_t *r) {
  return (r != NULL) && (r->w != 0U) && (r->h != 0U) &&
         ((uint32_t)r->x + r->w <= LCD_WIDTH) &&
         ((uint32_t)r->y + r->h <= LCD_HEIGHT);
}

/** @brief 用坐标与宽高构造矩形 */
static inline lcd_rect_t lcd_rect_make(uint16_t x, uint16_t y, uint16_t w,
                                       uint16_t h) {
  lcd_rect_t r = {x, y, w, h};
  return r;
}

/** @brief 整屏矩形 */
static inline lcd_rect_t lcd_rect_screen(void) {
  return lcd_rect_make(0U, 0U, (uint16_t)LCD_WIDTH, (uint16_t)LCD_HEIGHT);
}

/**
 * @brief 求两矩形交集
 * @param out 结果（可为 NULL，此时仅返回是否有交集）
 * @retval true  有交集（out 被写入）
 * @retval false 无交集（out 不被写入）
 */
bool lcd_rect_intersect(const lcd_rect_t *a, const lcd_rect_t *b,
                        lcd_rect_t *out);

/**
 * @brief 求两矩形的并集（紧致外接矩形）
 * @note  空矩形视为「单位元」：与空矩形求并得到另一矩形本身（不做裁剪）。
 */
void lcd_rect_union(const lcd_rect_t *a, const lcd_rect_t *b, lcd_rect_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LCD_TYPES_H */
