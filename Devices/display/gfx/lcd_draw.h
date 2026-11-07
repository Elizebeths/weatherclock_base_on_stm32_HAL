/**
 ******************************************************************************
 * @file    lcd_draw.h
 * @brief   L1 图形抽象层：**统一绘图 API**（点 / 线 / 矩形 / 圆 / 位图）
 *
 * @note    分层位置：L1（Devices/display/gfx）。所有原语只依赖画布
 *          （lcd_canvas），不含 HAL / STM32 符号。
 *
 * 统一语义（取代旧 lcd_gfx 的历史包袱）：
 *   · 坐标一律为 int32（便于表达负数与越界），**统一左闭右开**含义的游程；
 *   · **全部原语自带裁剪**，越界像素静默丢弃，永不写出画布/屏幕之外；
 *     旧实现「draw_point 返回错误 / draw_line 静默钳制 / draw_circle 整块拒绝」
 *     的三套语义在此统一。
 *   · **全部原语归约为水平/垂直游程**，因此在直写面板时也是「一次设窗 +
 *     一次连续提交」，而不是旧的「逐像素设窗」（那条路径一条屏宽线要几百次
 *     设窗，是本轮重构要消灭的主要性能瓶颈之一）。
 ******************************************************************************
 */

#ifndef LCD_DRAW_H
#define LCD_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "display/gfx/lcd_canvas.h"
#include "display/lcd_types.h"

/** @brief 用单色填充整个画布有效区 */
void lcd_draw_clear(lcd_canvas_t *c, lcd_color_t color);

/** @brief 画一个像素（越界忽略） */
void lcd_draw_pixel(lcd_canvas_t *c, int32_t x, int32_t y, lcd_color_t color);

/** @brief 画水平线段：起点 (x,y)，长度 w（像素），越界自动裁剪 */
void lcd_draw_hline(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t w,
                    lcd_color_t color);

/** @brief 画垂直线段：起点 (x,y)，长度 h（像素），越界自动裁剪 */
void lcd_draw_vline(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t h,
                    lcd_color_t color);

/**
 * @brief 画任意直线（整数 Bresenham，端点含，越界裁剪）
 * @note  内部按主轴分组成「水平或垂直游程」批量提交，不做逐像素设窗。
 */
void lcd_draw_line(lcd_canvas_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                   lcd_color_t color);

/** @brief 画矩形**描边**（1 像素宽） */
void lcd_draw_rect_outline(lcd_canvas_t *c, const lcd_rect_t *r, lcd_color_t color);

/** @brief 填充矩形 */
void lcd_fill_rect(lcd_canvas_t *c, const lcd_rect_t *r, lcd_color_t color);

/**
 * @brief 填充矩形（坐标用 int32，允许负坐标，自动裁剪）
 * @note  lcd_fill_rect 的放宽版：文本/图标在负坐标或跨越条带时使用。
 */
void lcd_draw_box(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t w, uint16_t h,
                  lcd_color_t color);

/**
 * @brief 画**实心圆**
 * @param cx,cy 圆心（可为负或越界，自动裁剪）
 * @param radius 半径（像素）
 * @note  逐行求半弦长（整数平方根）后以水平游程填充，复杂度 O(r) 次提交，
 *        取代旧实现「逐扫描线跑一次 Bresenham 直线」的 O(r) 次逐像素路径。
 */
void lcd_draw_circle_filled(lcd_canvas_t *c, int32_t cx, int32_t cy,
                            uint16_t radius, lcd_color_t color);

/**
 * @brief 绘制位图（RGB565，高字节在前，行优先）
 * @param x,y 左上角（可为负，自动裁剪）
 * @param w,h 位图尺寸
 * @param rgb565 像素数据
 */
void lcd_draw_bitmap(lcd_canvas_t *c, int32_t x, int32_t y, uint16_t w, uint16_t h,
                     const uint8_t *rgb565);

#ifdef __cplusplus
}
#endif

#endif /* LCD_DRAW_H */
