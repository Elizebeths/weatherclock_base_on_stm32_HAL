/**
 ******************************************************************************
 * @file    i2c_port.h
 * @brief   L0 I2C 总线抽象：主机收发 / mem 读写 / 地址扫描 / 毫秒超时 / 总线锁
 *
 * @note    分层位置：L0（Ports/bus）。**传感器模块与输入模块（电容触摸）共用
 *          本实现**，PH1 专题明确「L0 复用通信模块的 i2c_port，不重复计数」。
 *          本文件不得出现 HAL / STM32 符号。
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：iic_DHT20/Devices/dht20.c 的 HAL_I2C_Master_Transmit /
 *                    HAL_I2C_Master_Receive（第 19、41 行）与从机地址 0x70
 *                    （AHT20/DHT20 的 8 位形式，7 位为 0x38），
 *                    以及 Core/Src/i2c.c 的 I2C1 @ 100 kHz 配置
 *   一级 · 大型开源：ST 官方 STM32CubeF1 的 I2C 例程 —— 用状态返回值 +
 *                    HAL_I2C_GetError 回读错误位定位故障，而非无脑死等
 *   三级 · 小型开源：未使用
 *
 * ⚠ 对旧实现的两处修正（PH1 与传感器模块明令禁止的问题）：
 *   ① 旧 dht20.c 用 HAL_MAX_DELAY（0xFFFFFFFF）无限等待 —— 一旦从机不响应，
 *      整机将永久卡死。本接口所有事务都必须传毫秒超时（建议 APP_I2C_TIMEOUT_MS）。
 *   ② 旧 dht20.c 把 I2C 收发与 OLED 显示、HAL_Delay(500) 混在同一函数里 ——
 *      本接口只保留「总线语义」，显示与延时不进入 L0。
 *
 * ✅ 地址约定：本接口一律使用 **7 位地址**（AHT20 = 0x38），由实现内部左移为
 *    HAL 需要的 8 位形式（0x70）。
 ******************************************************************************
 */

#ifndef I2C_PORT_H
#define I2C_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 以 Ports/ 为根：只需 -I Ports 与 -I Config；依赖方向 总线 → 公共，单向 */
#include "common/port.h"

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

/**
 * @brief 确认 I2C 外设可用（幂等）
 * @param clk_hz 期望的 SCL 频率（Hz，如 APP_I2C_CLK_HZ）；传 0 表示不校验
 * @retval PORT_OK    就绪且频率符合期望（CubeMX 的 MX_I2C1_Init 已配置）
 * @retval PORT_ERROR 外设未初始化 / 工程未启用 I2C1
 * @note  与 spi_port 一致：外设配置归 CubeMX，本 Port 只做就绪校验，
 *        不调用 HAL_I2C_Init。
 */
port_status_t i2c_port_init(uint32_t clk_hz);

/** @brief I2C 外设是否就绪 */
bool i2c_port_is_ready(void);

/* ==========================================================================
 * 主机收发（7 位地址）
 * ========================================================================== */

/**
 * @brief 主机发送
 * @param addr7      7 位从机地址（AHT20 = 0x38）
 * @param buf        数据
 * @param len        字节数（0 < len <= 65535）
 * @param timeout_ms 超时（毫秒），必须非 0
 */
port_status_t i2c_port_write(uint8_t addr7, const uint8_t *buf, uint16_t len,
                             uint32_t timeout_ms);

/** @brief 主机接收 */
port_status_t i2c_port_read(uint8_t addr7, uint8_t *buf, uint16_t len,
                            uint32_t timeout_ms);

/**
 * @brief 先写后读（同一事务，中间不释放总线）
 * @note  AHT20/DHT20 的「发测量命令 → 等转换 → 读 6 字节」应尽量用本接口，
 *        避免其他使用者在这两步之间插入访问。
 */
port_status_t i2c_port_write_read(uint8_t addr7, const uint8_t *wbuf,
                                  uint16_t wlen, uint8_t *rbuf, uint16_t rlen,
                                  uint32_t timeout_ms);

/* ==========================================================================
 * 寄存器型器件（为后续 SHT30 / CST816S / AT24Cxx 预留）
 * ========================================================================== */

/**
 * 寄存器地址宽度。
 * 刻意定义中性枚举而非直接用 HAL 的 I2C_MEMADD_SIZE_8BIT/16BIT ——
 * 否则 L1 的器件驱动为了传这个参数就得包含 HAL 头文件，直接破坏分层。
 */
typedef enum {
  I2C_PORT_MEMADDR_8BIT = 0,
  I2C_PORT_MEMADDR_16BIT
} i2c_port_memaddr_size_t;

/** @brief 写寄存器：START → addr → mem_addr → data */
port_status_t i2c_port_mem_write(uint8_t addr7, uint16_t mem_addr,
                                 i2c_port_memaddr_size_t mem_addr_size,
                                 const uint8_t *buf, uint16_t len,
                                 uint32_t timeout_ms);

/** @brief 读寄存器：START → addr → mem_addr → RESTART → addr → data */
port_status_t i2c_port_mem_read(uint8_t addr7, uint16_t mem_addr,
                                i2c_port_memaddr_size_t mem_addr_size,
                                uint8_t *buf, uint16_t len,
                                uint32_t timeout_ms);

/* ==========================================================================
 * 探测与扫描（设备在线状态，供 L1 的 probe 与 PH1 的 dev_registry 使用）
 * ========================================================================== */

/**
 * @brief 探测某地址是否有器件应答
 * @param addr7      7 位地址
 * @param trials     重试次数（建议 2~3；旧 dht20.c 无此机制）
 * @param timeout_ms 每次尝试的超时（毫秒）
 * @retval PORT_OK      器件应答
 * @retval PORT_TIMEOUT 无应答
 */
port_status_t i2c_port_probe(uint8_t addr7, uint32_t trials, uint32_t timeout_ms);

/**
 * @brief 扫描总线上所有应答的 7 位地址
 * @param found     输出数组
 * @param max_count 数组容量
 * @return 实际发现的器件数（<= max_count）
 * @note  扫描较慢（每个地址一次探测），仅用于上电自检与排障，不放进主循环。
 */
uint8_t i2c_port_scan(uint8_t *found, uint8_t max_count);

/* ==========================================================================
 * 总线锁
 * ========================================================================== */

/**
 * @brief 获取总线锁（可重入，带超时）
 * @note  裸机实现为「嵌套计数 + 极短临界区改计数 + 自旋等待」，不长时间关中断；
 *        PH4 引入 FreeRTOS 后替换为递归互斥量，签名不变。
 */
void i2c_port_lock(void);

/** @brief 释放总线锁（与 i2c_port_lock() 配对） */
void i2c_port_unlock(void);

/** @brief 当前锁嵌套深度（0 = 未被持有，诊断用） */
uint32_t i2c_port_lock_depth(void);

/* ==========================================================================
 * 诊断
 * ========================================================================== */

/** @brief 总线索引（board.h 的 BOARD_I2C_BUS_INDEX），供日志区分多总线 */
uint8_t i2c_port_bus_index(void);

/** @brief 实际 SCL 频率（Hz），由 hi2c1.Init.ClockSpeed 读出 */
uint32_t i2c_port_clock_hz(void);

/**
 * @brief 上一次失败的原始错误码
 * @return 0 表示无错误；否则为 HAL 的 HAL_I2C_ERROR_* 位组合的**中性副本**
 * @note  为避免在头文件里暴露 HAL 常量，这里只返回位值本身，
 *        由 L1/L2 的日志层决定如何解释（HAL_I2C_ERROR_AF = 0x04 等）。
 */
uint32_t i2c_port_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_PORT_H */
