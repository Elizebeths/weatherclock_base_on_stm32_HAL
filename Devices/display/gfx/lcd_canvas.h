/**
 ******************************************************************************
 * @file    lcd_canvas.h
 * @brief   L1 图形抽象层：**绘制目标（画布）** —— 统一绘图 API 的基础
 *
 * @note    分层位置：L1（Devices/display/gfx）。依赖 lcd_types 与驱动层
 *          panel_st7789；不含 HAL / STM32 符号。
 *
 * 画布解决的三个问题：
 *   1) **统一绘图目标**：所有原语都写「画布」，上层不再关心像素是直接进面板
 *      还是先进内存条带；局部刷新与直写共用同一套绘制代码。
 *   2) **统一裁剪**：画布自带有效区域（bounds），越界像素被静默丢弃，
 *      不再出现「有的接口报错、有的钳制、有的整块拒绝」的混乱语义。
 *   3) **低 RAM 合成**：画布可以是内存里的一小块条带（宽度 x 若干行），
 *      在没有整屏帧缓冲（F103 只有 20 KB SRAM）的前提下实现「先在 RAM
 *      合成、再一次性提交」，从根上消除逐段涂写造成的闪烁。
 *
 * 坐标约定：画布 API 一律使用**屏幕坐标**（左上角原点），画布内部
 *          按 bounds 偏移映射到缓冲区。上层因此感知不到条带的存在。
 *
 * 后端（两个）：
 *   · buf == NULL  → **直写面板**：每个水平游程一次设窗 + 一次填充提交；
 *   · buf != NULL  → **内存条带**：写入 stride 为行距的缓冲，零总线流量。
 ******************************************************************************
 */

#ifndef LCD_CANVAS_H
#define LCD_CANVAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "display/lcd_types.h"

/** @brief 绘制目标 */
typedef struct {
  uint8_t *buf;      /**< 像素缓冲起点；NULL 表示直写面板 */
  uint16_t stride;   /**< 每行像素数（buf 模式有效） */
  lcd_rect_t bounds; /**< 屏幕坐标系下的有效区域（同时作为裁剪框） */
  lcd_err_t err;     /**< 最近一次后端错误（用于向上传播总线失败） */
} lcd_canvas_t;

/**
 * @brief 构造「直写面板」画布
 * @param c    画布
 * @param area 有效区域（屏幕坐标）；为 NULL 表示整屏
 * @note  直写模式适合一次性绘制（开机自检、全屏清屏）：逐游程立即上屏，
 *        没有缓冲、没有额外 RAM，但每次游程都是一次总线提交。
 */
void lcd_canvas_direct(lcd_canvas_t *c, const lcd_rect_t *area);

/**
 * @brief 构造「内存条带」画布
 * @param c      画布
 * @param buf    像素缓冲（RGB565，高字节在前，行优先）
 * @param stride 每行像素数
 * @param area   有效区域（屏幕坐标，即该条带在屏上的位置与尺寸）
 */
void lcd_canvas_buffer(lcd_canvas_t *c, uint8_t *buf, uint16_t stride,
                       const lcd_rect_t *area);

/**
 * @brief 把矩形裁剪到画布有效区域内
 * @param c   画布
 * @param r   待裁剪矩形（屏幕坐标）
 * @param out 结果（可为 NULL，此时仅判断是否有可见部分）
 * @retval true  裁剪后非空
 * @retval false 完全在画布外（或输入为空矩形）
 */
bool lcd_canvas_clip(const lcd_canvas_t *c, const lcd_rect_t *r,
                     lcd_rect_t *out);

/**
 * @brief 写入一段水平像素
 * @param r 目标区域；**必须已裁剪到画布 bounds 内**（由 lcd_draw_* 保证）
 * @note  这是画布的后端原语：直写模式 = 一次设窗 + 一次填充提交；
 *        条带模式 = 一段内存写入。所有绘制原语最终都归约到本接口。
 */
void lcd_canvas_rect(lcd_canvas_t *c, const lcd_rect_t *r, lcd_color_t color);

/**
 * @brief 把一块 RGB565 源数据写入画布
 * @param c             画布
 * @param r             目标区域；**必须已裁剪到画布 bounds 内**
 * @param src           源数据起点（RGB565，高字节在前，行优先）
 * @param src_stride_px 源数据每行像素数（用于裁剪后按行取数）
 * @note  直写模式走驱动层 panel_blit，此时要求 src_stride_px == r->w
 *        （否则置 c->err = LCD_ERR_UNSUPPORTED）；条带模式按行搬运，
 *        支持任意行距，因此裁剪开销为零拷贝。
 */
void lcd_canvas_blit(lcd_canvas_t *c, const lcd_rect_t *r, const uint8_t *src,
                     uint16_t src_stride_px);

#ifdef __cplusplus
}
#endif

#endif /* LCD_CANVAS_H */
