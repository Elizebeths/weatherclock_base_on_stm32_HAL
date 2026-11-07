/**
 ******************************************************************************
 * @file    lcd.h
 * @brief   L1 显示模块聚合门面（应用层的唯一入口）
 *
 * @note    分层位置：L1（Devices/display）。应用只需 #include "display/lcd.h"
 *          一次，即可获得类型、设备、画布、绘图、文本与刷新调度的全部接口。
 *          本文件不得出现任何 HAL / STM32 符号。
 *
 * @note    门面**刻意不暴露**驱动层内部头（panel_st7789 / st7789_regs）与
 *          脏区表内部头（lcd_dirty）：它们是实现细节，应用不应依赖。
 *          旧 lcd.h 直接包含 panel/lcd_panel.h，把驱动层泄露给了应用，
 *          本次重构已收敛。
 *
 * 依赖方向（严格单向）：
 *   app_display → lcd_text / lcd_draw → lcd_canvas → panel_st7789 → lcd_port
 *   app_display → lcd_refresh → lcd_dirty / panel_st7789 / os_port
 *   lcd_refresh → lcd_canvas（回调参数）
 ******************************************************************************
 */

#ifndef LCD_H
#define LCD_H

#ifdef __cplusplus
extern "C" {
#endif

/* 基础类型与错误码 */
#include "display/lcd_types.h"

/* 设备生命周期（初始化 / 背光 / 总线状态） */
#include "display/lcd_dev.h"

/* 图形抽象层：绘制目标、统一绘图 API、局部刷新调度 */
#include "display/gfx/lcd_canvas.h"
#include "display/gfx/lcd_draw.h"
#include "display/gfx/lcd_refresh.h"

/* 文本层：字体描述符与文本渲染 */
#include "display/text/lcd_font.h"
#include "display/text/lcd_text.h"

#ifdef __cplusplus
}
#endif

#endif /* LCD_H */
