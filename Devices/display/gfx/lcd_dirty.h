/**
 ******************************************************************************
 * @file    lcd_dirty.h
 * @brief   L1 图形抽象层：**脏矩形管理器**（定长表 + 合并 + 溢出兜底）
 *
 * @note    分层位置：L1（Devices/display/gfx）。纯逻辑，无 HAL / STM32 符号。
 *
 * 职责：只记录「屏幕的哪一块被改动过」，不关心怎么画、怎么传。
 *   · 定长表（DISPLAY_DIRTY_MAX 项，默认 8 x 8 B = 64 B），零动态分配；
 *   · 新增矩形与已有项相交则合并（反复合并至稳定）；
 *   · 表满时选择「并集面积增量最小」的项合并，避免一次溢出就膨胀成全屏；
 *   · 一律与屏幕范围求交，屏外部分被丢弃。
 *
 * 线程安全：本模块**不加锁**，由调用方（lcd_refresh）在临界区中调用。
 ******************************************************************************
 */

#ifndef LCD_DIRTY_H
#define LCD_DIRTY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "display/lcd_types.h"

/** @brief 清空脏区表 */
void lcd_dirty_reset(void);

/** @brief 脏区表是否为空 */
bool lcd_dirty_is_empty(void);

/** @brief 当前脏区数量 */
uint32_t lcd_dirty_count(void);

/**
 * @brief 取第 index 个脏区
 * @retval true  成功（out 被写入）
 * @retval false 下标越界或 out 为空
 */
bool lcd_dirty_get(uint32_t index, lcd_rect_t *out);

/**
 * @brief 登记一个被改动的矩形
 * @param r 目标区域（屏幕坐标；允许部分/全部越界，越界部分被丢弃）
 * @retval true  已登记（含被合并/丢弃）
 * @retval false 参数为空或矩形为空
 */
bool lcd_dirty_add(const lcd_rect_t *r);

/** @brief 登记整屏脏区 */
void lcd_dirty_add_screen(void);

/**
 * @brief 取出全部脏区并清空表（快照语义）
 * @param out       输出数组
 * @param capacity  out 的容量
 * @param count     实际写出的数量
 * @retval true  成功
 * @retval false 参数非法
 * @note  快照 + 清空使「渲染过程中产生的新脏区」留到下一轮，避免重入死循环。
 */
bool lcd_dirty_take_all(lcd_rect_t *out, uint32_t capacity, uint32_t *count);

#ifdef __cplusplus
}
#endif

#endif /* LCD_DIRTY_H */
