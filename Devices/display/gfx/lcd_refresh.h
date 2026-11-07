/**
 ******************************************************************************
 * @file    lcd_refresh.h
 * @brief   L1 图形抽象层：**局部刷新调度器**（脏区 → 条带渲染 → DMA 提交）
 *
 * @note    分层位置：L1（Devices/display/gfx）。依赖 lcd_dirty / lcd_canvas /
 *          驱动层 panel_st7789 与 L0 的 os_port；不含 HAL / STM32 符号。
 *
 * 刷新模型（无整屏帧缓冲，RAM 增量 < 2 KB）：
 *   应用只做两件事——「重绘回调」与「标脏」：
 *     1) 通过 lcd_refresh_init() 注册回调 void render(area, target)；
 *     2) 内容变化时调用 lcd_invalidate(rect) 标脏。
 *   调度器把脏区按 DISPLAY_STRIP_LINES 行切成水平条带，逐条：
 *     构造条带画布 → 调用 render(area, canvas) → 一次性 DMA 提交该条带。
 *   因此每帧只重绘「真正被标脏的区域」，静态区域零开销；且整块条带一次性
 *   上屏，不会出现逐段涂写造成的可见闪烁。
 *
 * 运行方式：
 *   · APP_OS_FREERTOS == 1：lcd_refresh_init 创建显示任务，任务阻塞等待
 *     「有脏区」信号量后自动冲刷；应用无需关心时机。
 *   · APP_OS_FREERTOS == 0（默认）：任务不创建，由 app_loop() 周期调用
 *     lcd_refresh_poll() 驱动同一套冲刷逻辑。
 *   两种方式对外接口完全一致。
 ******************************************************************************
 */

#ifndef LCD_REFRESH_H
#define LCD_REFRESH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "display/gfx/lcd_canvas.h"
#include "display/lcd_types.h"

/**
 * @brief 屏幕重绘回调：把 area 内的内容重新画到 target 上
 * @param area   需要重绘的矩形（屏幕坐标）；**回调必须完整覆盖该区域**
 * @param target 绘制目标（通常是一块条带）；调用方使用屏幕坐标绘图，
 *               越界部分由画布自动裁剪
 */
typedef void (*lcd_render_cb_t)(const lcd_rect_t *area, lcd_canvas_t *target);

/**
 * @brief 初始化刷新调度器
 * @param on_render 重绘回调（不可为 NULL）
 * @retval LCD_OK        就绪（含 RTOS 模式下任务创建成功）
 * @retval LCD_ERR_PARAM 回调为空
 * @retval LCD_ERR_STATE 任务创建失败
 * @note  必须在 panel_init() 之后调用；RTOS 模式下还必须在 osKernelInitialize()
 *        之后调用（即 main.c 完成内核初始化、启动调度器之前）。
 */
lcd_err_t lcd_refresh_init(lcd_render_cb_t on_render);

/**
 * @brief 标记一块区域需要重绘（可在任意任务上下文调用，内部加锁）
 * @param r 矩形（屏幕坐标）；允许部分越界，越界部分被丢弃
 * @note  RTOS 模式下会唤醒显示任务；裸机模式下标记后由 lcd_refresh_poll 处理。
 *        同一区域反复标脏会被自动合并，不会重复刷新。
 */
lcd_err_t lcd_invalidate(const lcd_rect_t *r);

/** @brief 标记整屏需要重绘（首帧/换页/清屏后使用） */
lcd_err_t lcd_invalidate_screen(void);

/**
 * @brief 立即冲刷当前全部脏区（同步，直到提交完成）
 * @note  通常不需要显式调用（RTOS 下由显示任务自动处理，裸机下由 poll 驱动）；
 *        首帧或需要「立即上屏」时使用。
 */
lcd_err_t lcd_refresh_flush(void);

/**
 * @brief 裸机驱动入口：由 app_loop() 周期调用
 * @note  RTOS 模式下为空操作（刷新由显示任务负责），因此可无条件调用。
 */
void lcd_refresh_poll(void);

/** @brief 当前是否存在未处理的脏区 */
bool lcd_refresh_is_dirty(void);

#ifdef __cplusplus
}
#endif

#endif /* LCD_REFRESH_H */
