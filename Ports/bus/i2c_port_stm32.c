/**
 ******************************************************************************
 * @file    i2c_port_stm32.c
 * @brief   L0 I2C 总线抽象 —— STM32F1 实现（hi2c1 的唯一持有者）
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：dht20.c 的 HAL_I2C_Master_Transmit / Master_Receive 用法
 *                    （但超时由 HAL_MAX_DELAY 改为毫秒参数）与地址 0x70→0x38
 *   一级 · 大型开源：ST 官方 STM32CubeF1 的 I2C 例程
 *                    （HAL_I2C_IsDeviceReady 做在线探测、
 *                      HAL_I2C_GetError 回读 AF/BERR/ARLO/TIMEOUT 定位故障、
 *                      HAL_I2C_Mem_Write/Read 用于寄存器型器件）
 *   一级 · 大型开源：FreeRTOS-Kernel 互斥量语义（总线锁，PH4 替换实现不改签名）
 *   三级 · 小型开源：未使用
 ******************************************************************************
 */

#include "i2c_port.h"

#include "board.h"
#include "i2c.h"  /* CubeMX 生成：extern I2C_HandleTypeDef hi2c1 */
#include "main.h" /* HAL —— 仅 L0 实现文件可包含 */

/* ==========================================================================
 * 私有状态
 * ========================================================================== */

/** 本层独占使用 I2C1；传感器与触摸共用这一条总线，由总线锁串行化 */
static I2C_HandleTypeDef *const s_i2c = &hi2c1;

static uint32_t s_last_error = 0U;
static uint32_t s_lock_depth = 0U;

/* ==========================================================================
 * 内部工具
 * ========================================================================== */

/** @brief 7 位地址 → HAL 需要的 8 位形式（左移一位，R/W 位由 HAL 填） */
static uint16_t addr_to_hal(uint8_t addr7) {
  return (uint16_t)((uint16_t)addr7 << 1U);
}

/** @brief HAL 状态 → 中性状态码，并留存原始错误位供日志定位 */
static port_status_t map_status(HAL_StatusTypeDef st) {
  switch (st) {
  case HAL_OK:
    return PORT_OK;
  case HAL_BUSY:
    return PORT_BUSY;
  case HAL_TIMEOUT:
    s_last_error = HAL_I2C_GetError(s_i2c);
    return PORT_TIMEOUT;
  case HAL_ERROR:
  default:
    s_last_error = HAL_I2C_GetError(s_i2c);
    return PORT_ERROR;
  }
}

/** @brief 可重入总线锁（与 spi_port 同一范式；PH4 换成递归互斥量） */
static void bus_lock(void) {
  port_enter_critical();
  s_lock_depth++;
  port_exit_critical();
}

static void bus_unlock(void) {
  port_enter_critical();
  if (s_lock_depth > 0U) {
    s_lock_depth--;
  }
  port_exit_critical();
}

/* ==========================================================================
 * 生命周期
 * ========================================================================== */

port_status_t i2c_port_init(uint32_t clk_hz) {
  if (s_i2c->Instance != I2C1) {
    return PORT_ERROR;
  }
  if (HAL_I2C_GetState(s_i2c) == HAL_I2C_STATE_RESET) {
    return PORT_ERROR; /* 未调用 MX_I2C1_Init() */
  }
  if ((clk_hz != 0U) && (s_i2c->Init.ClockSpeed != clk_hz)) {
    s_last_error = s_i2c->Init.ClockSpeed; /* 记录实际值，便于发现 .ioc 配错 */
    return PORT_ERROR;
  }
  return PORT_OK;
}

bool i2c_port_is_ready(void) {
  return (s_i2c->Instance == I2C1) &&
         (HAL_I2C_GetState(s_i2c) != HAL_I2C_STATE_RESET);
}

/* ==========================================================================
 * 主机收发
 * ========================================================================== */

port_status_t i2c_port_write(uint8_t addr7, const uint8_t *buf, uint16_t len,
                             uint32_t timeout_ms) {
  if ((buf == NULL) || (len == 0U) || (timeout_ms == 0U) ||
      !i2c_port_is_ready()) {
    return PORT_ERROR;
  }
  bus_lock();
  const port_status_t st = map_status(HAL_I2C_Master_Transmit(
      s_i2c, addr_to_hal(addr7), (uint8_t *)buf, len, timeout_ms));
  bus_unlock();
  return st;
}

port_status_t i2c_port_read(uint8_t addr7, uint8_t *buf, uint16_t len,
                            uint32_t timeout_ms) {
  if ((buf == NULL) || (len == 0U) || (timeout_ms == 0U) ||
      !i2c_port_is_ready()) {
    return PORT_ERROR;
  }
  bus_lock();
  const port_status_t st = map_status(
      HAL_I2C_Master_Receive(s_i2c, addr_to_hal(addr7), buf, len, timeout_ms));
  bus_unlock();
  return st;
}

port_status_t i2c_port_write_read(uint8_t addr7, const uint8_t *wbuf,
                                  uint16_t wlen, uint8_t *rbuf, uint16_t rlen,
                                  uint32_t timeout_ms) {
  if ((wbuf == NULL) || (wlen == 0U) || (rbuf == NULL) || (rlen == 0U) ||
      (timeout_ms == 0U) || !i2c_port_is_ready()) {
    return PORT_ERROR;
  }

  /* 两步之间不释放总线锁，避免其他使用者插入访问。
   * 注：F1 HAL 的阻塞式 API 无法在同一次传输里生成真正的重复起始条件，
   *     但 AHT20/DHT20 与 SHT30 都允许「写 → STOP → 读」的两段式访问，
   *     因此这里用锁保证原子性即可；若某器件强制要求重复起始，应使用
   *     HAL 的 IT/DMA 版顺序传输 API 实现（本阶段无此需求）。 */
  bus_lock();
  port_status_t st = map_status(
      HAL_I2C_Master_Transmit(s_i2c, addr_to_hal(addr7), (uint8_t *)wbuf, wlen,
                              timeout_ms));
  if (st == PORT_OK) {
    st = map_status(HAL_I2C_Master_Receive(s_i2c, addr_to_hal(addr7), rbuf,
                                          rlen, timeout_ms));
  }
  bus_unlock();
  return st;
}

/* ==========================================================================
 * 寄存器型器件
 * ========================================================================== */

/** @brief 中性寄存器宽度枚举 → HAL 常量（HAL 符号只出现在本实现文件里） */
static uint16_t memaddr_size_to_hal(i2c_port_memaddr_size_t size) {
  return (size == I2C_PORT_MEMADDR_16BIT) ? I2C_MEMADD_SIZE_16BIT
                                          : I2C_MEMADD_SIZE_8BIT;
}

port_status_t i2c_port_mem_write(uint8_t addr7, uint16_t mem_addr,
                                 i2c_port_memaddr_size_t mem_addr_size,
                                 const uint8_t *buf, uint16_t len,
                                 uint32_t timeout_ms) {
  if ((buf == NULL) || (len == 0U) || (timeout_ms == 0U) ||
      (mem_addr_size > I2C_PORT_MEMADDR_16BIT) || !i2c_port_is_ready()) {
    return PORT_ERROR;
  }
  bus_lock();
  const port_status_t st = map_status(
      HAL_I2C_Mem_Write(s_i2c, addr_to_hal(addr7), mem_addr,
                        memaddr_size_to_hal(mem_addr_size), (uint8_t *)buf,
                        len, timeout_ms));
  bus_unlock();
  return st;
}

port_status_t i2c_port_mem_read(uint8_t addr7, uint16_t mem_addr,
                                i2c_port_memaddr_size_t mem_addr_size,
                                uint8_t *buf, uint16_t len,
                                uint32_t timeout_ms) {
  if ((buf == NULL) || (len == 0U) || (timeout_ms == 0U) ||
      (mem_addr_size > I2C_PORT_MEMADDR_16BIT) || !i2c_port_is_ready()) {
    return PORT_ERROR;
  }
  bus_lock();
  const port_status_t st = map_status(
      HAL_I2C_Mem_Read(s_i2c, addr_to_hal(addr7), mem_addr,
                       memaddr_size_to_hal(mem_addr_size), buf, len,
                       timeout_ms));
  bus_unlock();
  return st;
}

/* ==========================================================================
 * 探测与扫描
 * ========================================================================== */

port_status_t i2c_port_probe(uint8_t addr7, uint32_t trials,
                             uint32_t timeout_ms) {
  if ((trials == 0U) || (timeout_ms == 0U) || !i2c_port_is_ready() ||
      (addr7 > 0x7FU)) {
    return PORT_ERROR;
  }
  bus_lock();
  const port_status_t st = map_status(HAL_I2C_IsDeviceReady(
      s_i2c, addr_to_hal(addr7), trials, timeout_ms));
  bus_unlock();
  return (st == PORT_OK) ? PORT_OK : PORT_TIMEOUT;
}

uint8_t i2c_port_scan(uint8_t *found, uint8_t max_count) {
  if ((found == NULL) || (max_count == 0U) || !i2c_port_is_ready()) {
    return 0U;
  }

  uint8_t count = 0U;
  for (uint16_t addr = APP_I2C_SCAN_ADDR_MIN;
       (addr <= APP_I2C_SCAN_ADDR_MAX) && (count < max_count); addr++) {
    /* 单次尝试 + 短超时：扫描是排障手段，不应让上电过程变慢 */
    if (i2c_port_probe((uint8_t)addr, 1U, APP_I2C_TIMEOUT_MS) == PORT_OK) {
      found[count++] = (uint8_t)addr;
    }
  }
  return count;
}

/* ==========================================================================
 * 总线锁 / 诊断
 * ========================================================================== */

void i2c_port_lock(void) { bus_lock(); }

void i2c_port_unlock(void) { bus_unlock(); }

uint32_t i2c_port_lock_depth(void) { return s_lock_depth; }

uint8_t i2c_port_bus_index(void) { return (uint8_t)BOARD_I2C_BUS_INDEX; }

uint32_t i2c_port_clock_hz(void) { return s_i2c->Init.ClockSpeed; }

uint32_t i2c_port_last_error(void) { return s_last_error; }
