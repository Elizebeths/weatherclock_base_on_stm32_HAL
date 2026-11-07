/**
 ******************************************************************************
 * @file    lcd_dev.h
 * @brief   L1 设备层：显示设备的生命周期编排（初始化 / 背光 / 总线状态）
 *
 * @note    分层位置：L1（Devices/display）。只编排驱动层（panel_st7789）与
 *          L0（lcd_port）的引脚/延时；**不含任何控制器寄存器细节**——
 *          初始化序列已数据化到 st7789_regs，本层不再知道 ST7789 的任何命令。
 *          不含 HAL / STM32 符号。
 ******************************************************************************
 */

#ifndef LCD_DEV_H
#define LCD_DEV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "display/lcd_types.h"

/**
 * @brief 初始化显示设备：就绪校验 → 关背光 → 面板复位与寄存器序列 → 打开显示
 * @retval LCD_OK        就绪
 * @retval LCD_ERR_STATE 底层显示 Port / 总线未就绪
 * @retval LCD_ERR_BUS   寄存器写入失败（总线门闩已置位）
 * @note  调用前必须完成 GPIO 与外设初始化（CubeMX 生成的 MX_*_Init）。
 * @note  **不含背光点亮**：调用方应在首帧上屏后再调用 lcd_bl_on()，
 *        以避免「上电白屏 → 首帧闪入」的可见跳变。
 * @note  幂等（重复调用只是重跑一遍时序，不会破坏状态）。
 */
lcd_err_t lcd_dev_init(void);

/** @brief 打开背光（true = 点亮，极性由 Config/board.h 决定） */
void lcd_bl_on(void);

/** @brief 关闭背光 */
void lcd_bl_off(void);

/** @brief 是否发生过总线传输失败（true 表示屏上内容不可信） */
bool lcd_is_bus_error(void);

/** @brief 清除总线错误门闩（确认恢复后调用） */
void lcd_dev_clear_bus_error(void);

#ifdef __cplusplus
}
#endif

#endif /* LCD_DEV_H */
