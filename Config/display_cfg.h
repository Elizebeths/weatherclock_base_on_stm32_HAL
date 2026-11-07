/**
 ******************************************************************************
 * @file    display_cfg.h
 * @brief   显示模块编译期配置：几何、面板偏移、条带/填充缓冲、脏区表容量
 *
 * @note    分层位置：Config/（编译期配置）。**零依赖、零包含**——
 *          不引用任何 Ports/ 头文件，因此 L1（Devices/display）可以只依赖
 *          本文件取得几何，而不再经 board.h 被拖入引脚/GPIO 层
 *          （旧链路 lcd_types.h → board.h → gpio/gpio_port.h 已被切断）。
 *
 * @note    本文件是显示几何与缓冲尺寸的**唯一事实来源**：
 *          换屏（改分辨率/控制器的显存偏移）只改这里。
 ******************************************************************************
 */

#ifndef DISPLAY_CFG_H
#define DISPLAY_CFG_H

/* ==========================================================================
 * 一、屏幕几何（像素）
 * ========================================================================== */
#define DISPLAY_WIDTH  240U /**< 可见区宽度 */
#define DISPLAY_HEIGHT 320U /**< 可见区高度 */

/**
 * 面板控制器显存相对可见区的偏移（列/行）。
 * 240x320 模块为 0/0；170x320 模块需要行偏移 34（面板 RAM 240x320，
 * 可见区只有 170 列 x 320 行，上下各被遮挡 34 行）。
 * 偏移在驱动层（panel_st7789）的窗口设置里统一叠加，上层坐标永远是
 * 「可见区坐标」，换屏不影响任何绘制代码。
 */
#define DISPLAY_PANEL_COL_OFFSET 0U
#define DISPLAY_PANEL_ROW_OFFSET 0U

/* ==========================================================================
 * 二、刷新（脏矩形 + 条带缓冲，无整屏帧缓冲）
 * ========================================================================== */

/**
 * 条带缓冲行数。刷新时把脏区按本高度切成水平条带，逐条渲染并提交。
 * RAM 开销 = DISPLAY_WIDTH * DISPLAY_STRIP_LINES * DISPLAY_BPP
 *   2 行 → 240 * 2 * 2 = 960 B（默认，适配 20 KB SRAM）
 *   4 行 → 1920 B；8 行 → 3840 B（须自行核对 SRAM 预算）
 */
#define DISPLAY_STRIP_LINES 2U

/**
 * 脏区表容量。绘制接口只登记被改动的矩形，刷新时合并后一次性提交。
 * 每项 8 字节（4 个 uint16_t），8 项 = 64 B。
 * 合并后仍溢出时，退化为「整屏标脏」（正确性优先，性能次之）。
 */
#define DISPLAY_DIRTY_MAX 8U

/**
 * 单色填充的临时行数：填充一块矩形时，先构造本行数的纯色像素，
 * 再靠面板地址自增重复提交。RAM 开销 = WIDTH * LINES * BPP。
 */
#define DISPLAY_FILL_LINES 1U

/* ==========================================================================
 * 三、像素格式与提交
 * ========================================================================== */
#define DISPLAY_BPP 2U /**< RGB565：每像素 2 字节 */

/**
 * 单次 DMA 提交的字节上限。HAL SPI 的 DMA 传输长度为 uint16_t，
 * 故必须 <= 65535；取 240 像素宽（480 B）的整数倍，保证分块落在像素边界上。
 *   128 * 480 = 61440 B
 */
#define DISPLAY_DMA_BLOCK_MAX 61440U

/**
 * 编译期几何自检：条带/填充缓冲必须至少 1 行，脏区表非空。
 * 用负长度数组触发编译错误（C99），比运行期断言更早暴露配置错误。
 */
typedef char display_cfg_strip_nonzero[(DISPLAY_STRIP_LINES > 0U) ? 1 : -1];
typedef char display_cfg_fill_nonzero[(DISPLAY_FILL_LINES > 0U) ? 1 : -1];
typedef char display_cfg_dirty_nonzero[(DISPLAY_DIRTY_MAX > 0U) ? 1 : -1];
/** 单块 DMA 提交上限（HAL SPI DMA 的 Size 为 uint16_t） */
typedef char display_cfg_dma_block_fits_u16[(DISPLAY_WIDTH * DISPLAY_BPP <= 65535U)
                                                ? 1
                                                : -1];

#endif /* DISPLAY_CFG_H */
