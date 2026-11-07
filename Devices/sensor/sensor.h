/**
 ******************************************************************************
 * @file    sensor.h
 * @brief   L1 传感器模块统一抽象：设备句柄 + init / probe / read 函数表
 *
 * @note    分层位置：L1（Devices/sensor）。本文件不得出现任何 HAL / STM32 符号，
 *          只依赖 L0 的中性接口（common/port.h 的状态码）。
 *          DHT20 是它的第一个实现；后续增补 SHT30 时只需新增
 *          Devices/sensor/sht30.c 并填一份 sensor_ops_t，
 *          L2/L3 的调用代码一行不改。
 *
 * 适配来源
 *   一级 · 大型开源：Linux iio / 嵌入式「设备 + ops 函数表」范式 ——
 *                    把「对传感器问什么」与「怎么说话」分离，型号差异被关在 ops 里
 *   二级 · 自有模块：iic_DHT20/Devices/dht20.h 的 DHT20_Device 结构体
 *                    （iic 句柄 + addr + Temp/Humi）—— 保留「句柄 + 数据」的组织
 *                    形式，但把 HAL 句柄换成中性 addr7，把 int 字段换成带量纲的
 *                    定点值，避免上层猜单位（旧代码的 Temp/Humi 是整数摄氏度与
 *                    整数百分比，精度被丢掉了一半）
 *   三级 · 小型开源：未使用
 *
 * 为什么包装函数是 static inline：
 *   本层刻意只保留「一个公共头 + 各型号一个 .c」，与 lcd.h 的聚合门面保持一致，
 *   不额外引入 sensor.c 与对应的 stub。C99 下内联包装零开销，还省掉了每个调用点
 *   重复判空、重复解引用的样板代码。
 ******************************************************************************
 */

#ifndef SENSOR_H
#define SENSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/port.h"

/* ==========================================================================
 * 一、采样值（中性量纲：0.1 单位，与具体型号的原始位宽解耦）
 * ========================================================================== */

/** 合法温度下限（-40.0 ℃），与 DHT20 / SHT30 的标称量程一致 */
#define SENSOR_TEMP_C_X10_MIN (-400)
/** 合法温度上限（+80.0 ℃） */
#define SENSOR_TEMP_C_X10_MAX (800)
/** 合法相对湿度上限（100.0 %RH，下限为 0） */
#define SENSOR_HUMI_RH_X10_MAX (1000U)

/**
 * @brief 一次采样结果
 * @note  统一以 0.1 为单位输出，保留小数点后一位：
 *        temp_c_x10 = 265 表示 26.5 ℃；humi_rh_x10 = 512 表示 51.2 %RH。
 *        旧实现用 int 存整数摄氏度/百分比，本抽象把精度提高一档。
 */
typedef struct {
  int32_t temp_c_x10;   /**< 温度，单位 0.1 ℃ */
  uint32_t humi_rh_x10; /**< 相对湿度，单位 0.1 %RH */
} sensor_sample_t;

/** @brief 采样值是否落在器件的合法量程内（供上层做合理性校验） */
static inline bool sensor_sample_valid(const sensor_sample_t *s) {
  return (s != NULL) && (s->temp_c_x10 >= SENSOR_TEMP_C_X10_MIN) &&
         (s->temp_c_x10 <= SENSOR_TEMP_C_X10_MAX) &&
         (s->humi_rh_x10 <= SENSOR_HUMI_RH_X10_MAX);
}

/* ==========================================================================
 * 二、设备句柄与型号实现
 * ========================================================================== */

typedef struct sensor_dev sensor_dev_t;

/**
 * @brief 型号实现函数表
 * @note  init 必须幂等；read 内部负责「触发测量 → 等待转换 → 取回结果」的完整时序，
 *        并保证所有事务都带毫秒超时（禁止无限等待）。
 *        三者的返回值统一为 port_status_t，便于上层用同一套错误处理。
 */
typedef struct {
  const char *name; /**< 型号名（如 "DHT20"），供日志与自检输出 */
  /** @brief 上电等待 + 校准检查，必要时下发初始化命令；幂等 */
  port_status_t (*init)(sensor_dev_t *dev);
  /** @brief 器件是否在线（应答地址） */
  port_status_t (*probe)(sensor_dev_t *dev);
  /** @brief 触发一次测量并取回结果（阻塞至多 APP_DHT20_CONVERT_TIMEOUT_MS） */
  port_status_t (*read)(sensor_dev_t *dev, sensor_sample_t *out);
} sensor_ops_t;

/**
 * @brief 传感器设备句柄
 * @note  ctx 由各型号自行解释（本阶段 DHT20 不需要私有状态，置 NULL）。
 *        句柄由型号实现以 static 存储提供（见 dht20_device()），
 *        上层只持有指针，不负责分配/释放。
 */
struct sensor_dev {
  const sensor_ops_t *ops; /**< 型号实现，非空表示已绑定 */
  uint8_t addr7;           /**< 7 位从机地址（实现内部左移成 HAL 的 8 位形式） */
  void *ctx;               /**< 型号私有上下文，可为 NULL */
};

/* ==========================================================================
 * 三、统一调用入口（对 ops 的安全包装）
 * ========================================================================== */

/** @brief 句柄是否已绑定型号实现 */
static inline bool sensor_dev_bound(const sensor_dev_t *dev) {
  return (dev != NULL) && (dev->ops != NULL) && (dev->ops->read != NULL);
}

/** @brief 型号名（未绑定或未命名时返回 "unknown"，可直接进日志） */
static inline const char *sensor_dev_name(const sensor_dev_t *dev) {
  return sensor_dev_bound(dev) && (dev->ops->name != NULL) ? dev->ops->name
                                                           : "unknown";
}

/** @brief 初始化传感器（未绑定实现时返回 PORT_ERROR，不产生空指针解引用） */
static inline port_status_t sensor_dev_init(sensor_dev_t *dev) {
  if (!sensor_dev_bound(dev) || (dev->ops->init == NULL)) {
    return PORT_ERROR;
  }
  return dev->ops->init(dev);
}

/** @brief 探测传感器是否在线 */
static inline port_status_t sensor_dev_probe(sensor_dev_t *dev) {
  if (!sensor_dev_bound(dev) || (dev->ops->probe == NULL)) {
    return PORT_ERROR;
  }
  return dev->ops->probe(dev);
}

/** @brief 读取一次采样（out 为空时返回 PORT_ERROR，不做部分写入） */
static inline port_status_t sensor_dev_read(sensor_dev_t *dev,
                                            sensor_sample_t *out) {
  if (!sensor_dev_bound(dev) || (out == NULL)) {
    return PORT_ERROR;
  }
  return dev->ops->read(dev, out);
}

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_H */
