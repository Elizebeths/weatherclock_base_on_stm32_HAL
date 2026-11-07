/**
 ******************************************************************************
 * @file    dht20.c
 * @brief   L1 DHT20/AHT20 温湿度驱动实现（全部经 L0 的 i2c_port 访问总线）
 *
 * @note    分层位置：L1（Devices/sensor）。本文件不含任何 HAL / STM32 符号：
 *          I2C 收发与超时由 i2c_port 承担，等待由 port_delay_ms 承担，
 *          时序与阈值来自 app_config.h，从机地址来自 board.h。
 *
 * 适配来源
 *   二级 · 自有模块：iic_DHT20 的 dht20.c —— 命令序列（0x71 / 0xBE 08 00 /
 *                    0xAC 33 00）、状态字 busy 位与 20 位原始值的换算公式均沿用；
 *                    同时修掉它的三处硬伤：
 *                    ① HAL_MAX_DELAY 无限等待 → 全部改 APP_I2C_TIMEOUT_MS
 *                    ② HAL_Delay(500) 固定长延时 → 改「最短静默 + 就绪轮询 + 上限」
 *                    ③ 未读第 7 字节、未校验 CRC → 本次补齐 CRC8
 *   一级 · 大型开源：Adafruit AHT20 库的校准判定（状态字需含 0x18）与
 *                    CRC8（多项式 0x31、初值 0xFF、覆盖前 6 字节）
 *   一级 · 大型开源：STM32CubeF1 的 HAL_I2C_IsDeviceReady 探测范式（经 i2c_port_probe）
 *   三级 · 小型开源：未使用
 *
 * 总线占用说明：i2c_port 的每次收发内部自带总线锁，函数返回即释放；
 *   本驱动的「等待转换」发生在两次事务之间，因此不持锁 —— 共用的 I2C1
 *   （未来的 CST816S 触摸）不会被 80 ms 的转换期挡住。
 ******************************************************************************
 */

#include "sensor/dht20.h"

#include <stddef.h>

#include "app_config.h"
#include "board.h"
#include "bus/i2c_port.h"

/* ==========================================================================
 * 一、命令与时序常量
 * ========================================================================== */

#define DHT20_CMD_STATUS  0x71U /**< 读状态字（1 字节） */
#define DHT20_CMD_INIT    0xBEU /**< 初始化/校准命令首字节 */
#define DHT20_CMD_MEASURE 0xACU /**< 触发测量命令首字节 */

#define DHT20_MEASURE_ARG0 0x33U /**< 0xAC 0x33 0x00：触发温湿度测量 */
#define DHT20_MEASURE_ARG1 0x00U
#define DHT20_INIT_ARG0    0x08U /**< 0xBE 0x08 0x00：使能校准 */
#define DHT20_INIT_ARG1    0x00U

/**
 * 状态字掩码。
 * 规范要求 bit[4]|bit[3] = 0x18 才算就绪/已校准（旧实现判的是 0x04 = bit[2]，
 * 属误写；此处按 Adafruit AHT20 库的判定修正，并保留 bit[3] 作为最终判据）。
 */
#define DHT20_STATUS_CAL_MASK 0x18U
#define DHT20_STATUS_CAL_BIT  0x08U
/** 测量结果首字节 bit[7]：1 = 仍在转换，0 = 已完成且数据有效 */
#define DHT20_STATUS_BUSY_BIT 0x80U

/** 下发 0xBE 0x08 0x00 后的建立时间(ms) */
#define DHT20_CAL_WAIT_MS 10U

/** 参与 CRC 的数据长度：状态 1 B + 温度/湿度 5 B */
#define DHT20_DATA_LEN 6U
/** 一次完整读回的字节数：数据 6 B + CRC 1 B */
#define DHT20_READ_LEN (DHT20_DATA_LEN + 1U)

/** 20 位原始值满量程 2^20，以及用于四舍五入的半量程 2^19 */
#define DHT20_RAW_ROUND_HALF (1UL << 19)

/** 换算系数：湿度 0.1 %RH 满量程 1000，温度 0.1 ℃ 满量程 2000（偏移 -500 = -50 ℃） */
#define DHT20_HUMI_FULL_SCALE_X10 1000UL
#define DHT20_TEMP_FULL_SCALE_X10 2000L
#define DHT20_TEMP_OFFSET_X10 500

/** 配置自检：轮询窗口必须能覆盖最短静默等待，否则首轮读即被判超时 */
APP_STATIC_ASSERT((APP_DHT20_CONVERT_MIN_MS <= APP_DHT20_CONVERT_TIMEOUT_MS),
                  dht20_convert_window);

/* ==========================================================================
 * 二、设备句柄（型号实现以静态存储提供，上层只持指针）
 * ========================================================================== */

static port_status_t dht20_ops_init_(sensor_dev_t *dev);
static port_status_t dht20_ops_probe_(sensor_dev_t *dev);
static port_status_t dht20_ops_read_(sensor_dev_t *dev, sensor_sample_t *out);

static const sensor_ops_t s_dht20_ops = {
    .name = "DHT20",
    .init = dht20_ops_init_,
    .probe = dht20_ops_probe_,
    .read = dht20_ops_read_,
};

static sensor_dev_t s_dht20_dev = {
    .ops = &s_dht20_ops,
    /* board.h 只登记 7 位地址；i2c_port 内部左移成 HAL 需要的 0x70 */
    .addr7 = BOARD_AHT20_I2C_ADDR7,
    .ctx = NULL,
};

/**
 * 上电稳定等待是否已完成。
 * 「上电等待」是电源事件而非 init 事件：只做一次，保证 init 真正的幂等
 * （否则自检与业务各调一次 init 就会白等两个 APP_DHT20_POWER_ON_DELAY_MS）。
 */
static bool s_powered_ = false;

/** @brief 失败出口：清掉上电标志，让下一次重试重新走完整上电等待 */
static port_status_t dht20_fail_(port_status_t st) {
  s_powered_ = false;
  return st;
}

/* ==========================================================================
 * 三、内部工具
 * ========================================================================== */

/**
 * @brief CRC8：多项式 0x31、初值 0xFF、不反转、无最终异或
 * @note  DHT20/AHT20 规格给出的校验算法，覆盖结果的前 6 字节
 */
static uint8_t dht20_crc8_(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0xFFU;
  for (uint8_t i = 0U; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0U; bit < 8U; bit++) {
      crc = ((crc & 0x80U) != 0U) ? (uint8_t)((crc << 1U) ^ 0x31U)
                                  : (uint8_t)(crc << 1U);
    }
  }
  return crc;
}

/**
 * @brief 读状态字：0x71 命令 + 1 字节回读
 * @note  用 write_read 让「命令 → 回读」在同一把总线锁内完成，避免他人插入
 */
static port_status_t dht20_read_status_(uint8_t *status) {
  const uint8_t cmd = DHT20_CMD_STATUS;
  return i2c_port_write_read(s_dht20_dev.addr7, &cmd, 1U, status, 1U,
                             APP_I2C_TIMEOUT_MS);
}

/* ==========================================================================
 * 四、对外 API
 * ========================================================================== */

port_status_t dht20_init(void) { return dht20_ops_init_(&s_dht20_dev); }

port_status_t dht20_probe(void) { return dht20_ops_probe_(&s_dht20_dev); }

port_status_t dht20_read(sensor_sample_t *out) {
  return dht20_ops_read_(&s_dht20_dev, out);
}

sensor_dev_t *dht20_device(void) { return &s_dht20_dev; }

uint8_t dht20_addr7(void) { return s_dht20_dev.addr7; }

/* ==========================================================================
 * 五、型号实现（sensor_ops_t）
 * ========================================================================== */

static port_status_t dht20_ops_probe_(sensor_dev_t *dev) {
  if (dev == NULL) {
    return PORT_ERROR;
  }
  /* 多重试几次覆盖上电抖动；i2c_port_probe 内部只回 PORT_OK / PORT_TIMEOUT */
  return i2c_port_probe(dev->addr7, APP_DHT20_PROBE_TRIALS,
                        APP_I2C_TIMEOUT_MS);
}

static port_status_t dht20_ops_init_(sensor_dev_t *dev) {
  if (dev == NULL) {
    return PORT_ERROR;
  }

  /* 就绪校验：确认 CubeMX 已生成 MX_I2C1_Init 且时钟符合 app_config 期望 */
  if (i2c_port_init(APP_I2C_CLK_HZ) != PORT_OK) {
    return PORT_ERROR;
  }

  /* 上电稳定等待（DHT20 要求 >=40 ms，默认取 100 ms，见 app_config.h）。
   * 只在首次（或上次失败后）执行，使重复 init 不产生额外阻塞。 */
  if (!s_powered_) {
    port_delay_ms(APP_DHT20_POWER_ON_DELAY_MS);
    s_powered_ = true;
  }

  uint8_t status = 0U;
  port_status_t st = dht20_read_status_(&status);
  if (st != PORT_OK) {
    return dht20_fail_(st);
  }

  if ((status & DHT20_STATUS_CAL_MASK) == DHT20_STATUS_CAL_MASK) {
    return PORT_OK; /* 已校准，无需下发命令（幂等：重复调用走这一支） */
  }

  /* 未校准：下发 0xBE 0x08 0x00 并等待建立 */
  const uint8_t init_cmd[3] = {DHT20_CMD_INIT, DHT20_INIT_ARG0,
                               DHT20_INIT_ARG1};
  st = i2c_port_write(dev->addr7, init_cmd, (uint16_t)sizeof(init_cmd),
                      APP_I2C_TIMEOUT_MS);
  if (st != PORT_OK) {
    return dht20_fail_(st);
  }
  port_delay_ms(DHT20_CAL_WAIT_MS);

  st = dht20_read_status_(&status);
  if (st != PORT_OK) {
    return dht20_fail_(st);
  }
  /* 校准位仍未置起说明器件异常（接线/供电/型号不符），如实上报而非假装成功 */
  return ((status & DHT20_STATUS_CAL_BIT) != 0U) ? PORT_OK
                                                 : dht20_fail_(PORT_ERROR);
}

static port_status_t dht20_ops_read_(sensor_dev_t *dev, sensor_sample_t *out) {
  if ((dev == NULL) || (out == NULL)) {
    return PORT_ERROR;
  }

  const uint8_t cmd[3] = {DHT20_CMD_MEASURE, DHT20_MEASURE_ARG0,
                          DHT20_MEASURE_ARG1};
  uint8_t raw[DHT20_READ_LEN] = {0};

  /* ① 触发测量：单次事务，返回即释放总线锁 */
  port_status_t st =
      i2c_port_write(dev->addr7, cmd, (uint16_t)sizeof(cmd), APP_I2C_TIMEOUT_MS);
  if (st != PORT_OK) {
    return st;
  }

  /* ② 先按规格最短转换时间静默等待，再以固定步长轮询 busy 位。
   *    等待期间不持有总线锁，共用 I2C1 的其他模块不受影响。
   *    与旧实现相比：固定 500 ms → 典型 80 ms，且仍保留超时上限。 */
  port_delay_ms(APP_DHT20_CONVERT_MIN_MS);
  uint32_t waited_ms = APP_DHT20_CONVERT_MIN_MS;
  for (;;) {
    st = i2c_port_read(dev->addr7, raw, (uint16_t)sizeof(raw),
                       APP_I2C_TIMEOUT_MS);
    if (st != PORT_OK) {
      return st;
    }
    if ((raw[0] & DHT20_STATUS_BUSY_BIT) == 0U) {
      break; /* bit[7] = 0 → 转换完成，数据有效 */
    }
    if (waited_ms >= APP_DHT20_CONVERT_TIMEOUT_MS) {
      return PORT_TIMEOUT;
    }
    port_delay_ms(APP_DHT20_POLL_INTERVAL_MS);
    waited_ms += APP_DHT20_POLL_INTERVAL_MS;
  }

  /* ③ 完整性校验（旧实现完全未做）：第 7 字节是前 6 字节的 CRC8 */
#if (APP_DHT20_CRC_ENABLE)
  if (dht20_crc8_(raw, DHT20_DATA_LEN) != raw[DHT20_DATA_LEN]) {
    return PORT_ERROR;
  }
#endif

  /* ④ 解析 20 位原始值：湿度 = raw[1..2] + raw[3] 高半字节；
   *    温度 = raw[3] 低半字节 + raw[4..5] */
  const uint32_t raw_humi = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) |
                            ((uint32_t)raw[3] >> 4);
  const uint32_t raw_temp = (((uint32_t)raw[3] & 0x0FU) << 16) |
                            ((uint32_t)raw[4] << 8) | (uint32_t)raw[5];

  /* ⑤ 换算为 0.1 单位，并做四舍五入（整数截断会稳定偏低半个刻度）。
   *    用 64 位中间量：raw 最大 0xFFFFF，×2000 = 2.10e9 已逼近 int32 上限，
   *    显式提位宽后无需再担心溢出。 */
  out->humi_rh_x10 = (uint32_t)(((uint64_t)raw_humi * DHT20_HUMI_FULL_SCALE_X10 +
                                 DHT20_RAW_ROUND_HALF) >>
                                20);
  out->temp_c_x10 =
      (int32_t)(((uint64_t)raw_temp * (uint64_t)DHT20_TEMP_FULL_SCALE_X10 +
                 DHT20_RAW_ROUND_HALF) >>
                20) -
      DHT20_TEMP_OFFSET_X10;

  /* ⑥ 量程合理性校验：CRC 通过也可能因器件异常给出越界值，不向上层透传 */
  return sensor_sample_valid(out) ? PORT_OK : PORT_ERROR;
}
