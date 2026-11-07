/**
 ******************************************************************************
 * @file    dht20.h
 * @brief   L1 DHT20/AHT20 温湿度驱动接口（Devices/sensor/ 的第一个型号实现）
 *
 * @note    分层位置：L1（Devices/sensor）。本文件不得出现任何 HAL / STM32 符号，
 *          只依赖 L0 的 i2c_port（收发/超时/探测）与 sensor.h（统一抽象）。
 *
 * 适配来源
 *   二级 · 自有模块：iic_DHT20 的 dht20.c —— 保留其**命令与时序知识**：
 *                    0x71 读状态字、0xBE 0x08 0x00 初始化、0xAC 0x33 0x00 触发测量、
 *                    以及「20 位原始值 → 温湿度」的换算公式；但**不保留**其
 *                    HAL_MAX_DELAY 无限等待、HAL_Delay(500) 固定长延时、
 *                    以及把显示与延时混进总线函数里的写法。
 *   一级 · 大型开源：Adafruit AHT20 库的校准判定（状态字 0x18）与
 *                    CRC8（多项式 0x31、初值 0xFF、覆盖前 6 字节）校验
 *   三级 · 小型开源：未使用
 *
 * 用法（两种等价写法）
 *   ① 型号直呼：dht20_init() / dht20_read(&sample)
 *   ② 经统一抽象：sensor_dev_init(dht20_device()) / sensor_dev_read(dht20_device(), &sample)
 *   第二种写法在换型号时不需要改任何调用点。
 ******************************************************************************
 */

#ifndef DHT20_H
#define DHT20_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sensor/sensor.h"

/**
 * @brief 初始化 DHT20：上电等待 → 读状态字 → 必要时下发 0xBE 0x08 0x00 → 校验校准位
 * @retval PORT_OK      已校准，可立即测量
 * @retval PORT_ERROR   I2C 未就绪 / 校准位始终未置起
 * @retval PORT_TIMEOUT 从机不应答
 * @note  幂等。上电等待时长取 APP_DHT20_POWER_ON_DELAY_MS；
 *        调用前需完成 MX_I2C1_Init()（内部会幂等确认 i2c_port 就绪）。
 */
port_status_t dht20_init(void);

/**
 * @brief 探测 DHT20 是否在线
 * @retval PORT_OK      应答（地址 0x38）
 * @retval PORT_TIMEOUT 无应答
 * @note  重试次数取 APP_DHT20_PROBE_TRIALS，覆盖上电抖动。
 */
port_status_t dht20_probe(void);

/**
 * @brief 触发一次测量并取回温湿度
 * @param out 输出，单位见 sensor_sample_t（0.1 ℃ / 0.1 %RH）
 * @retval PORT_OK      测量成功且 CRC 校验通过
 * @retval PORT_TIMEOUT 转换在 APP_DHT20_CONVERT_TIMEOUT_MS 内未完成
 * @retval PORT_ERROR   参数非法 / CRC 不符 / 总线错误
 * @note  阻塞时长约 APP_DHT20_CONVERT_MIN_MS；
 *        等待转换期间**不持有总线锁**，不会阻塞共用 I2C1 的其他模块（如后续触摸）。
 */
port_status_t dht20_read(sensor_sample_t *out);

/**
 * @brief 取 DHT20 的设备句柄（供 sensor.h 的统一抽象使用）
 * @return 指向内部静态句柄的指针，永不为 NULL
 */
sensor_dev_t *dht20_device(void);

/** @brief DHT20 的 7 位从机地址（board.h 的 BOARD_AHT20_I2C_ADDR7，供日志/自检输出） */
uint8_t dht20_addr7(void);

#ifdef __cplusplus
}
#endif

#endif /* DHT20_H */
