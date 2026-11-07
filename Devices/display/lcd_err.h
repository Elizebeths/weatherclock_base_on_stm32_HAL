/**
 ******************************************************************************
 * @file    lcd_err.h
 * @brief   L1 显示模块统一错误码（中性，不含任何 HAL / STM32 类型）
 *
 * @note    分层位置：L1（Devices/display）。只依赖 stdint/stdbool。
 *          从旧 lcd_dev.c 中抽出，使「错误码」成为一个可被各子层独立包含的
 *          最小单元，避免为了一个枚举而拖入设备层实现。
 ******************************************************************************
 */

#ifndef LCD_ERR_H
#define LCD_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 显示模块错误码 */
typedef enum {
  LCD_OK = 0,        /**< 成功 */
  LCD_ERR_PARAM,     /**< 参数非法（越界 / 空指针） */
  LCD_ERR_BUS,       /**< 总线传输失败（对应 L0 的总线门闩） */
  LCD_ERR_STATE,     /**< 状态非法（未初始化等） */
  LCD_ERR_BUSY,      /**< 资源忙（上一次提交尚未完成） */
  LCD_ERR_UNSUPPORTED /**< 本实现不支持该操作 */
} lcd_err_t;

/**
 * @brief 错误码转可读字符串
 * @return 静态常量字符串（不参与内存管理，可安全长期持有）
 */
const char *lcd_err_str(lcd_err_t err);

#ifdef __cplusplus
}
#endif

#endif /* LCD_ERR_H */
