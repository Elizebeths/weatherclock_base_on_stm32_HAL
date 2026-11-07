/**
 ******************************************************************************
 * @file    lcd_font_ascii16x32.h
 * @brief   16x32 ASCII 点阵字库数据声明（0x20~0x7E，共 95 字符）
 *
 * @note    分层位置：L1（Devices/display/text）。纯数据声明，无依赖。
 *
 * 字模存放格式（与源库逐字节一致）：
 *   · 每字符 64 字节 = 16 列 x 32 行，行优先；
 *   · 每行 2 字节（左 8 像素、右 8 像素），字节内 bit0 为最左像素；
 *   · **上下镜像存放**（原库配套面板 MY=1）。本工程面板为 MY=0，
 *     因此由 lcd_text 在取模时按行倒序读取（见 lcd_font_t.row_inverted）。
 ******************************************************************************
 */

#ifndef LCD_FONT_ASCII16X32_H_INCLUDED
#define LCD_FONT_ASCII16X32_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define LCD_FONT_ASCII16X32_W     16U   /**< 字模宽（像素） */
#define LCD_FONT_ASCII16X32_H     32U   /**< 字模高（像素） */
#define LCD_FONT_ASCII16X32_FIRST ' '   /**< 首个可显示字符 0x20 */
#define LCD_FONT_ASCII16X32_LAST  '~'   /**< 末个可显示字符 0x7E */
#define LCD_FONT_ASCII16X32_COUNT 95U   /**< 字符总数 */
#define LCD_FONT_ASCII16X32_BYTES_PER_CHAR 64U /**< 每字符字节数（16*32/8） */
#define LCD_FONT_ASCII16X32_BYTES_PER_ROW  2U  /**< 每行字节数（16/8） */

/** @brief 16x32 点阵字模：95 字符，每字符 64 字节 */
extern const unsigned char ascii_1632[LCD_FONT_ASCII16X32_COUNT]
                                     [LCD_FONT_ASCII16X32_BYTES_PER_CHAR];

#ifdef __cplusplus
}
#endif

#endif /* LCD_FONT_ASCII16X32_H_INCLUDED */
