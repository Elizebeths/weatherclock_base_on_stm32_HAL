/**
 ******************************************************************************
 * @file    st7789_regs.h
 * @brief   ST7789 类控制器命令码、参数与**初始化序列表**
 *
 * @note    分层位置：L1（Devices/display/panel）。纯数据声明，无依赖，
 *          不含 HAL / STM32 符号。
 *
 * 与旧实现的差别：旧代码把「发哪条命令、发什么参数、等多少毫秒」的**顺序**
 * 硬编码在设备层（lcd_dev_init），导致换控制器必须改设备层。现在顺序被建模为
 * 数据表 ST7789_INIT_SEQ，由驱动层（panel_st7789.c）遍历执行——
 * 设备层不再知道任何控制器细节。
 ******************************************************************************
 */

#ifndef ST7789_REGS_H
#define ST7789_REGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ---- 命令码（单字节） ---- */
extern const uint8_t ST7789_SLPOUT;  /**< 0x11 退出睡眠 */
extern const uint8_t ST7789_INVON;   /**< 0x21 打开反显 */
extern const uint8_t ST7789_DISPON;  /**< 0x29 打开显示 */
extern const uint8_t ST7789_CASET;   /**< 0x2A 列地址设置 */
extern const uint8_t ST7789_RASET;   /**< 0x2B 行地址设置 */
extern const uint8_t ST7789_RAMWR;   /**< 0x2C 写显存 */
extern const uint8_t ST7789_MADCTL;  /**< 0x36 显存访问控制 */
extern const uint8_t ST7789_COLMOD;  /**< 0x3A 像素格式（RGB565=0x05） */

/* ---- 命令参数（数组，声明处给出长度以便 sizeof 求得字节数） ---- */
extern const uint8_t ST7789_MADCTL_DAT[1];   /**< {0x00} MY=0，行地址 0 在顶部 */
extern const uint8_t ST7789_COLMOD_DAT[1];   /**< {0x05} 16bit/pixel */
extern const uint8_t ST7789_PORCTRL;         /**< 0xB2 前后沿 */
extern const uint8_t ST7789_PORCTRL_DAT[5];
extern const uint8_t ST7789_GCTRL;           /**< 0xB7 门控 */
extern const uint8_t ST7789_GCTRL_DAT[1];
extern const uint8_t ST7789_VCOMS;           /**< 0xBB VCOM */
extern const uint8_t ST7789_VCOMS_DAT[1];
extern const uint8_t ST7789_LCMCTRL;         /**< 0xC0 LCM 控制 */
extern const uint8_t ST7789_LCMCTRL_DAT[1];
extern const uint8_t ST7789_VDVVRHEN;        /**< 0xC2 VDV/VRH 使能 */
extern const uint8_t ST7789_VDVVRHEN_DAT[1];
extern const uint8_t ST7789_VRHS;            /**< 0xC3 VRH 设置 */
extern const uint8_t ST7789_VRHS_DAT[1];
extern const uint8_t ST7789_VDVS;            /**< 0xC4 VDV 设置 */
extern const uint8_t ST7789_VDVS_DAT[1];
extern const uint8_t ST7789_FRCTRL2;         /**< 0xC6 帧率 */
extern const uint8_t ST7789_FRCTRL2_DAT[1];
extern const uint8_t ST7789_PWCTRL1;         /**< 0xD0 电源控制 */
extern const uint8_t ST7789_PWCTRL1_DAT[2];
extern const uint8_t ST7789_PVGAMCTRL;       /**< 0xE0 正极性 Gamma */
extern const uint8_t ST7789_PVGAMCTRL_DAT[14];
extern const uint8_t ST7789_NVGAMCTRL;       /**< 0xE1 负极性 Gamma */
extern const uint8_t ST7789_NVGAMCTRL_DAT[14];

/** @brief 正/负极性 Gamma 参数长度（14 字节） */
#define ST7789_GAMMA_LEN 14U

/** @brief 占位命令：只执行延时，不向总线发送任何字节 */
#define ST7789_SEQ_DELAY_ONLY 0x00U

/* ---- 初始化步骤 ---- */

/**
 * @brief 初始化序列的一步
 * @note  cmd           命令码（ST7789_SEQ_DELAY_ONLY 时只延时）
 *        dat / len     参数字节（可为 NULL / 0）
 *        delay_ms      本步执行后需要等待的毫秒数（0 = 不等待）
 */
typedef struct {
  uint8_t cmd;
  const uint8_t *dat;
  uint8_t len;
  uint8_t delay_ms;
} st7789_init_step_t;

/** @brief 上电初始化序列（顺序即执行顺序；由 panel_st7789 遍历执行） */
extern const st7789_init_step_t ST7789_INIT_SEQ[];

/** @brief 初始化序列的步数 */
extern const uint16_t ST7789_INIT_SEQ_LEN;

/** @brief 硬件复位时序（毫秒）：先拉低 RESET_MS 再拉高，随后等待 SETTLE_MS */
#define ST7789_RESET_LOW_MS    20U
#define ST7789_RESET_SETTLE_MS 120U

#ifdef __cplusplus
}
#endif

#endif /* ST7789_REGS_H */
