/**
 ******************************************************************************
 * @file    lcd_port.h
 * @brief   L0 显示硬件抽象：SPI 收发、DMA 单块提交、引脚电平、延时、DWT 计时、临界区
 *
 * @note    分层位置：L0（Ports/display）。PH1 专题明文规定的「六类接口」即
 *          lcd_port_spi_tx / lcd_port_spi_tx_dma / lcd_port_pin_write /
 *          lcd_port_delay_ms / lcd_port_enter_critical / lcd_port_cycles，
 *          另加忙查询、中止与总线错误门闩（保持与旧驱动的错误语义等效）。
 *          本文件不得出现 HAL / STM32 符号。
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：lcdLib-of-240x320-SPI-TFT-ST7789/Devices/lcd.c —— 几乎全部
 *          内容由此迁移与抽象：
 *            - 第 29-36 行 LCD_CS/DC_HIGH/LOW() 四个宏 → lcd_port_pin_write()
 *            - 第 38-44 行 LCD_BL_on()/LCD_BL_off()        → lcd_port_pin_write(LCD_PIN_BL, …)
 *            - 第 68-76 行 LCD_transfer_()（HAL_SPI_Transmit + 100 ms 超时 +
 *              失败置 lcd_bus_error_）                    → lcd_port_spi_tx()
 *            - 第 112-129 行 LCD_writeDATStream_()（CS 全程拉低、按 4096 B 分块）
 *                                                          → 分块下沉到 spi_port，
 *                                                            CS 控制留在本层
 *            - 第 47-52 行 LCD_reset_() 的 HAL_Delay(20/120) → lcd_port_delay_ms()
 *   一级 · 大型开源：Bodmer/TFT_eSPI（约 3.5k star）的 ST7789 整块像素提交与
 *                    「DMA 期间保持 CS 拉低、传输完成后再释放」的时序策略
 *   一级 · 大型开源：LVGL port 层「提交 / 完成」分离的范式 —— 本接口只负责
 *                    **单块**搬运并在完成回调里通知上层，双缓冲与下一块策略
 *                    由 L1 的 lcd_panel 决定，不写进 L0
 *   一级 · 大型开源：ST 官方 STM32CubeF1（SPI 轮询/中断传输的状态机与错误码）
 *   三级 · 小型开源：未使用
 *
 * 设计决策：SPI 字节搬运由 Ports/bus/spi_port 统一承担（一个句柄、一把总线锁、
 * 一处 DMA 通道），本层只做「显示语义 + 引脚 + 提交完成回调」——
 * 这样后续 Storage 复用 spi_port 时不会出现两个模块争抢 hspi1 的问题。
 ******************************************************************************
 */

#ifndef LCD_PORT_H
#define LCD_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 以 Ports/ 为根：只需 -I Ports 与 -I Config；依赖方向 显示 → 公共，单向 */
#include "common/port.h"

/* ==========================================================================
 * 一、显示专用引脚
 * ========================================================================== */

typedef enum {
  LCD_PIN_CS = 0, /**< 片选，低有效 */
  LCD_PIN_DC,     /**< 数据/命令选择，低=命令，高=数据 */
  LCD_PIN_RST,    /**< 硬件复位，低有效 */
  LCD_PIN_BL,     /**< 背光；level=true 表示「点亮」，极性由 board.h 决定 */
  LCD_PIN_MAX
} lcd_port_pin_t;

/**
 * @brief 传输完成回调
 * @param status 传输结果（PORT_OK / PORT_TIMEOUT / PORT_ERROR）
 * @param arg    注册时传入的上下文
 * @note  在中断上下文（DMA 完成）或同步返回（回退轮询）时被调用。
 *        在此回调内提交下一块是允许的；但**不得改写刚刚传完的那块缓冲**
 *        之外的正在被 DMA 读取的缓冲。
 */
typedef void (*lcd_port_done_cb_t)(port_status_t status, void *arg);

/* ==========================================================================
 * 二、生命周期
 * ========================================================================== */

/**
 * @brief 初始化显示 Port：把 CS/DC/RST 置为安全电平、关闭背光，并确保
 *        底层 spi_port 就绪（幂等）。
 * @note  不包含面板复位时序 —— 复位由 L1 的 lcd_panel 用
 *        lcd_port_pin_write(LCD_PIN_RST, …) + lcd_port_delay_ms() 编排，
 *        这样换屏型时 L0 一行不改。
 * @retval PORT_OK    就绪
 * @retval PORT_ERROR SPI 外设未初始化（请确认 CubeMX 已生成 MX_SPI1_Init）
 */
port_status_t lcd_port_init(void);

/* ==========================================================================
 * 三、SPI 传输（PH1 六类接口之 1、2）
 * ========================================================================== */

/**
 * @brief 轮询发送一段数据（阻塞）
 * @param data 数据指针
 * @param len  字节数；内部按 app_config 的分块阈值多次搬运
 * @retval PORT_OK / PORT_TIMEOUT / PORT_ERROR / PORT_BUSY
 * @note  CS 在整段传输期间保持拉低，返回前拉高；DC 由调用方在此之前设置。
 * @note  这既是运行路径，也是 DMA 出错后的降级路径。
 */
port_status_t lcd_port_spi_tx(const uint8_t *data, uint16_t len);

/**
 * @brief 写「命令 + 可选参数」：整段共享**一次片选拉低**（一次 CS 会话）
 * @param cmd 命令字节（DC=0 期间发送）
 * @param dat 参数数据，可为 NULL
 * @param len 参数字节数；为 0 时只发命令
 * @retval PORT_OK / PORT_TIMEOUT / PORT_ERROR / PORT_BUSY
 * @note  相比「lcd_port_spi_tx(cmd) + lcd_port_spi_tx(dat)」的两次调用，
 *        本接口把命令与参数收敛到一次 CS 会话，减少片选翻转与事务数；
 *        返回前 DC 被恢复为数据态、CS 拉高。
 */
port_status_t lcd_port_cmd_dat(uint8_t cmd, const uint8_t *dat, uint16_t len);

/**
 * @brief 以 DMA 提交**单块**数据（非阻塞）
 * @param data 数据指针；**必须指向静态区或调用方长期持有的缓冲**，
 *             回调返回前不得改写
 * @param len  字节数（> 65535 请分层提交）
 * @param cb   完成回调，可为 NULL
 * @param arg  回调上下文
 * @retval PORT_OK  已提交（回调稍后触发）
 * @retval PORT_BUSY 上一次提交尚未完成
 * @note  DMA 不可用时自动回退为轮询并**同步**触发回调，对上层透明。
 * @note  整块传输期间 CS 保持拉低，回调触发前拉高 —— 满足「最后一块传完前
 *        拉高 CS 会丢像素」的时序要求。
 */
port_status_t lcd_port_spi_tx_dma(const uint8_t *data, uint32_t len,
                                  lcd_port_done_cb_t cb, void *arg);

/** @brief 是否有 DMA 传输正在进行 */
bool lcd_port_spi_busy(void);

/** @brief 中止正在进行的 DMA 传输并释放 CS（用于超时/异常恢复） */
void lcd_port_spi_abort(void);

/* ==========================================================================
 * 四、引脚 / 延时 / 计时 / 临界区（PH1 六类接口之 3、4、5、6）
 * ========================================================================== */

/**
 * @brief 写显示专用引脚
 * @param pin   引脚
 * @param level 逻辑电平。对 LCD_PIN_BL，true = 点亮（极性已在实现内处理）
 */
void lcd_port_pin_write(lcd_port_pin_t pin, bool level);

/** @brief 毫秒延时（转发 port_delay_ms，禁止上层直接调 HAL_Delay） */
void lcd_port_delay_ms(uint32_t ms);

/** @brief 进入临界区（可嵌套） */
void lcd_port_enter_critical(void);

/** @brief 退出临界区 */
void lcd_port_exit_critical(void);

/** @brief 取 DWT 周期计数，供 PH1 的信能埋点（S0 基线 / S3 复测） */
uint32_t lcd_port_cycles(void);

/* ==========================================================================
 * 五、总线错误门闩（与旧 LCD_isBusError() 语义等效）
 * ========================================================================== */

/** @brief 是否发生过传输失败（一旦置位，后续 SPI 调用立即返回不再等超时） */
bool lcd_port_is_bus_error(void);

/** @brief 清除错误门闩（恢复时调用） */
void lcd_port_clear_bus_error(void);

#ifdef __cplusplus
}
#endif

#endif /* LCD_PORT_H */
