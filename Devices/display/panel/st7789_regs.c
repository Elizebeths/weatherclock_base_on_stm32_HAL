/**
 ******************************************************************************
 * @file    st7789_regs.c
 * @brief   ST7789 类控制器初始化命令与参数序列（数据定义）
 *
 * @note    取值逐字节迁自 lcdLib-of-240x320-SPI-TFT-ST7789/Devices/lcd_font.c
 *          第 3-59 行，仅重命名，未改动任何数值。
 *          其中 MADCTL=0x00（MY=0，行地址 0 在屏幕顶部），配套的字模由 lcd_text
 *          按行倒序读取 —— 与源库一致。
 ******************************************************************************
 */

#include "display/panel/st7789_regs.h"

#include <stddef.h> /* NULL */

/* ---- 命令码 ---- */
const uint8_t ST7789_SLPOUT = 0x11;
const uint8_t ST7789_INVON = 0x21;
const uint8_t ST7789_DISPON = 0x29;
const uint8_t ST7789_CASET = 0x2A;
const uint8_t ST7789_RASET = 0x2B;
const uint8_t ST7789_RAMWR = 0x2C;
const uint8_t ST7789_MADCTL = 0x36;
const uint8_t ST7789_COLMOD = 0x3A;

/* ---- 命令参数 ---- */
/* 0x00: MY=0/MX=0/MV=0/BGR=0 —— 行地址 0 在屏幕顶部（自然方向） */
const uint8_t ST7789_MADCTL_DAT[] = {0x00};

const uint8_t ST7789_COLMOD_DAT[] = {0x05}; /* 16 bit/pixel, RGB565 */

const uint8_t ST7789_PORCTRL = 0xB2;
const uint8_t ST7789_PORCTRL_DAT[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};

const uint8_t ST7789_GCTRL = 0xB7;
const uint8_t ST7789_GCTRL_DAT[] = {0x35};

const uint8_t ST7789_VCOMS = 0xBB;
const uint8_t ST7789_VCOMS_DAT[] = {0x35};

const uint8_t ST7789_LCMCTRL = 0xC0;
const uint8_t ST7789_LCMCTRL_DAT[] = {0x2C};

const uint8_t ST7789_VDVVRHEN = 0xC2;
const uint8_t ST7789_VDVVRHEN_DAT[] = {0x01};

const uint8_t ST7789_VRHS = 0xC3;
const uint8_t ST7789_VRHS_DAT[] = {0x13};

const uint8_t ST7789_VDVS = 0xC4;
const uint8_t ST7789_VDVS_DAT[] = {0x20};

const uint8_t ST7789_FRCTRL2 = 0xC6;
const uint8_t ST7789_FRCTRL2_DAT[] = {0x0F};

const uint8_t ST7789_PWCTRL1 = 0xD0;
const uint8_t ST7789_PWCTRL1_DAT[] = {0xA4, 0xA1};

/* 注：若屏幕工作异常，可尝试追加 REG=0xD6 / DAT=0xA1（官方程序有、数据手册无） */
const uint8_t ST7789_PVGAMCTRL = 0xE0;
const uint8_t ST7789_PVGAMCTRL_DAT[] = {0xF0, 0x00, 0x04, 0x04, 0x04, 0x05, 0x29,
                                        0x33, 0x3E, 0x38, 0x12, 0x12, 0x28, 0x30};
const uint8_t ST7789_NVGAMCTRL = 0xE1;
const uint8_t ST7789_NVGAMCTRL_DAT[] = {0xF0, 0x07, 0x0A, 0x0D, 0x0B, 0x07, 0x28,
                                       0x33, 0x3E, 0x36, 0x14, 0x14, 0x29, 0x32};

/* ==========================================================================
 * 初始化序列（数据表驱动）
 *
 * 取值与顺序逐条对齐旧 lcd_dev_init 的寄存器序列，唯一差异：
 * 旧实现把 SLPOUT 发送了**两次**（初次唤醒一次、Gamma 之后又发一次），
 * 属冗余（SLPOUT 幂等）；此处保留一次并于原位置以「延时 120 ms」占位，
 * 使上电时序（含等待时长）与旧实现完全一致。
 * ========================================================================== */
const st7789_init_step_t ST7789_INIT_SEQ[] = {
    /* cmd                    dat                        len  delay(ms) */
    {ST7789_SLPOUT, NULL, 0U, 120U},
    {ST7789_MADCTL, ST7789_MADCTL_DAT, 1U, 0U},
    {ST7789_COLMOD, ST7789_COLMOD_DAT, 1U, 0U},
    {ST7789_PORCTRL, ST7789_PORCTRL_DAT, 5U, 0U},
    {ST7789_GCTRL, ST7789_GCTRL_DAT, 1U, 0U},
    {ST7789_VCOMS, ST7789_VCOMS_DAT, 1U, 0U},
    {ST7789_LCMCTRL, ST7789_LCMCTRL_DAT, 1U, 0U},
    {ST7789_VDVVRHEN, ST7789_VDVVRHEN_DAT, 1U, 0U},
    {ST7789_VRHS, ST7789_VRHS_DAT, 1U, 0U},
    {ST7789_VDVS, ST7789_VDVS_DAT, 1U, 0U},
    {ST7789_FRCTRL2, ST7789_FRCTRL2_DAT, 1U, 0U},
    {ST7789_PWCTRL1, ST7789_PWCTRL1_DAT, 2U, 0U},
    {ST7789_PVGAMCTRL, ST7789_PVGAMCTRL_DAT, ST7789_GAMMA_LEN, 0U},
    {ST7789_NVGAMCTRL, ST7789_NVGAMCTRL_DAT, ST7789_GAMMA_LEN, 0U},
    {ST7789_INVON, NULL, 0U, 0U},
    /* 旧实现在此再发一次 SLPOUT 并等 120 ms；此处只保留等待 */
    {ST7789_SEQ_DELAY_ONLY, NULL, 0U, 120U},
    {ST7789_DISPON, NULL, 0U, 0U},
};

const uint16_t ST7789_INIT_SEQ_LEN =
    (uint16_t)(sizeof(ST7789_INIT_SEQ) / sizeof(ST7789_INIT_SEQ[0]));

