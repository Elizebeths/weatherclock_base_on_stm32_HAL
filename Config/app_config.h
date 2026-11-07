/**
 ******************************************************************************
 * @file    app_config.h
 * @brief   编译期功能开关与阈值（周期 / 超时 / 分块 / 波特率 / 日志级别）
 *
 * @note    分层位置：Config/。任何层都可以包含本文件；本文件不包含任何头文件，
 *          以保持零依赖（避免 Config → Ports → Config 的循环包含）。
 ******************************************************************************
 *
 * 适配来源
 *   二级 · 自有模块：lcdLib 的 lcd.h「用户配置区」
 *                    （LCD_SPI_TIMEOUT_MS 100ms、LCD_SPI_CHUNK_BYTES 4096、
 *                      LCD_BL_ACTIVE_HIGH、LCD_WIDTH/HEIGHT、字库几何）
 *                    与 iic_DHT20 的 Core/Src/i2c.c（I2C1 @ 100 kHz）
 *   一级 · 大型开源：ST 官方 STM32CubeF1 的 UART DMA + IDLE 例程
 *                    （波特率 115200、IDLE 半收中断关闭的推荐做法）
 *
 * 对应《PH1 显示攻坚专题》的性能要求：
 *   SPI 预分频 4 → 2（18 → 36 MHz，按 18/27/36 MHz 分档压测，不稳回退并记录）
 ******************************************************************************
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ==========================================================================
 * 一、OS 适配（Ports/common/os_port）
 *
 * APP_OS_FREERTOS：
 *   0（默认）= 裸机等效实现 —— 互斥量为临界区计数、信号量为计数+忙等、
 *              任务只登记不创建；显示刷新由 app_loop() 轮询驱动。
 *   1        = FreeRTOS / CMSIS-RTOS2 实现 —— 显示刷新跑在独立任务里。
 *              置 1 的前提：已用 CubeMX 为工程加入 FREERTOS 中间件
 *              （生成 FreeRTOSConfig.h / cmsis_os2 头文件与内核源码），
 *              并在 main.c 中启动调度器（osKernelStart）。
 * 注意：FreeRTOS 头文件只允许出现在 Ports/common/os_port_stm32.c。
 * ========================================================================== */
#define APP_OS_FREERTOS 0

/** 显示刷新任务：栈深度（字）与优先级（数值越大越高） */
#define APP_DISPLAY_TASK_STACK_WORDS 192U
#define APP_DISPLAY_TASK_PRIORITY    4U

/** 刷新任务等待「有脏区」的最长阻塞时间(ms)；仅 RTOS 模式使用 */
#define APP_DISPLAY_DIRTY_TIMEOUT_MS 1000U

/* ==========================================================================
 * 二、显示 SPI（TFT / ST7789）
 * ========================================================================== */

/**
 * SPI 预分频指数：实际分频 = 2^N。N=1 → 36 MHz；N=2 → 18 MHz（不稳时回退）。
 * 注意：SPI 时钟由 CubeMX 生成的 MX_SPI1_Init() 设置（本宏不被代码引用），
 * 修改时须同步 .ioc 的 SPI1.BaudRatePrescaler，此处仅作记录以备核对。
 */
#define APP_SPI_LCD_PRESCALER_LOG2 1U

/** 轮询传输超时(ms)。不使用 HAL_MAX_DELAY，避免总线异常时整机假死 */
#define APP_SPI_LCD_TIMEOUT_MS 100U

/** 轮询路径的单次分块字节数（须 <= 65535；旧 lcd.h 的 LCD_SPI_CHUNK_BYTES） */
#define APP_SPI_LCD_CHUNK_BYTES 4096U

/** DMA 单块提交的字节数上限（HAL_SPI_Transmit_DMA 的 Size 为 uint16_t） */
#define APP_SPI_LCD_DMA_MAX_BYTES 65535U

#define APP_DISPLAY_PERIOD_INDOOR_MS 1000
#define APP_DISPLAY_PERIOD_OUTDOOR_MS 1000

/* ==========================================================================
 * 三、总线锁与 I2C
 * ========================================================================== */

/** 总线锁自旋等待上限(ms)；裸机下等价于「重入保护」，PH4 起换成互斥量超时 */
#define APP_BUS_LOCK_TIMEOUT_MS 50U

/** I2C1 时钟频率（Hz）。旧工程为 100 kHz；走线良好可提至 400000 */
#define APP_I2C_CLK_HZ 100000U

/** I2C 单次事务超时(ms)。替代旧 dht20.c 的 HAL_MAX_DELAY 死等 */
#define APP_I2C_TIMEOUT_MS 50U

/** I2C 地址扫描范围（7 位地址） */
#define APP_I2C_SCAN_ADDR_MIN 0x08U
#define APP_I2C_SCAN_ADDR_MAX 0x77U

/* ==========================================================================
 * 三·B、温湿度传感器（DHT20/AHT20，复用 I2C1）
 * 设计要点：旧 App/dht20 把 HAL_Delay(100) / HAL_Delay(500) 硬编码在函数体内，
 *           且用 HAL_MAX_DELAY 无限等待。此处把时序收敛为编译期常量，并以
 *           「轮询就绪 + 上限」替代固定长延时 —— 这是本次性能收益的主要来源。
 * ========================================================================== */

/**
 * 上电稳定等待(ms)。
 * DHT20 数据手册要求上电后 >=40 ms 才可通信；这里沿用旧实现验证过的 100 ms。
 * 若实测稳定可下调至 50，缩短上电到首帧的时延。
 */
#define APP_DHT20_POWER_ON_DELAY_MS 100U

/**
 * 单次转换的等待上限(ms)。DHT20 转换时间为 ~80 ms；
 * 调用方按 APP_DHT20_POLL_INTERVAL_MS 轮询就绪位，命中即返回，
 * 超时仍未就绪则判定失败（旧实现固定 HAL_Delay(500)，白等 400+ ms）。
 */
#define APP_DHT20_CONVERT_TIMEOUT_MS 100U

/**
 * 转换最短静默等待(ms)：先按器件规格（约 80 ms）等满，再开始轮询就绪位，
 * 避免在转换期内做无意义的空读 —— 典型情况 1 次读即可命中。
 * 必须 <= APP_DHT20_CONVERT_TIMEOUT_MS，否则驱动会直接把首轮读判为超时。
 */
#define APP_DHT20_CONVERT_MIN_MS 80U

/** 就绪轮询间隔(ms)：两次读状态之间的等待步长 */
#define APP_DHT20_POLL_INTERVAL_MS 10U

/** 器件在线探测的重试次数（覆盖上电抖动；旧实现无重试机制） */
#define APP_DHT20_PROBE_TRIALS 3U

/**
 * 是否校验测量结果尾字节 CRC8（多项式 0x31，初值 0xFF，覆盖前 6 字节）。
 * 置 0 可省去校验开销，但会丢失数据完整性保障；建议量产保持 1。
 */
#define APP_DHT20_CRC_ENABLE 1

/** 温湿度采样周期(ms)：正常运行时两次采样之间的间隔（自检路径不使用） */
#define APP_SENSOR_PERIOD_MS 10000U

#define APP_NET_PERIOD_MS 30000U

#define APP_NET_TIME_MS 1000U

/**
 * 是否启用传感器上电自检（语义与 APP_LCD_ENABLE_SELFTEST 一致）：
 * 扫描总线确认 0x38 在线 → 读取一次温湿度 → 分级日志输出；置 0 时为空实现。
 */
#define APP_SENSOR_ENABLE_SELFTEST 1

/* ==========================================================================
 * 四、UART
 * ========================================================================== */

/** 日志通道（USART1）波特率 */
#define APP_UART_LOG_BAUD 115200U

/** UART 轮询发送超时(ms) */
#define APP_UART_TX_TIMEOUT_MS 100U

/* ==========================================================================
 * 五、日志与调试
 * ========================================================================== */

/** 日志级别：0=OFF 1=ERROR 2=WARN 3=INFO 4=DEBUG 5=VERBOSE
 *  （由 PH1 系统基础的 log.c 与 EasyLogger 共用；取值与 ELOG_LVL_* 一一对应） */
#define APP_LOG_LEVEL_OFF     0
#define APP_LOG_LEVEL_ERROR   1
#define APP_LOG_LEVEL_WARN    2
#define APP_LOG_LEVEL_INFO    3
#define APP_LOG_LEVEL_DEBUG   4
#define APP_LOG_LEVEL_VERBOSE 5

/** 默认日志级别。
 *  EasyLogger 在编译期据此裁剪：低于该级别的 log_x() 会被展开成 ((void)0)，
 *  运行时 elog_set_filter_lvl() 再高也不会输出。调试阶段用 VERBOSE，
 *  量产前改回 INFO（可省 Flash 与运行开销）。 */
#define APP_LOG_LEVEL_DEFAULT APP_LOG_LEVEL_VERBOSE

/** 是否启用 DWT 周期计数（性能埋点；关闭后 port_cycles 返回 0） */
#define APP_PORT_DWT_ENABLE 1

/** 是否启用显示上电自检（PH1 的 app_display_selftest.c 用；产品版置 0）
 *  0 = 正常显示业务 UI（app_display_render 接管画面）
 *  1 = 上电自检图案，业务 UI 不接管 */
#define APP_LCD_ENABLE_SELFTEST 0

/* ==========================================================================
 * 六、GPIO / EXTI
 * ========================================================================== */

/** EXTI 中断的 NVIC 优先级（PH4 引入 FreeRTOS 后须 <= configMAX_SYSCALL_INTERRUPT_PRIORITY） */
#define APP_GPIO_IRQ_PRIORITY 5U

/** 同一时刻可注册的 EXTI 引脚数量上限（F1 上同号引脚共享 EXTI 线，故按线计数） */
#define APP_GPIO_IRQ_MAX 8U

/* 注：断言宏（APP_STATIC_ASSERT / APP_ASSERT）与 port_assert_failed() 定义在
 *     Ports/common/port.h 中 —— 它们依赖 port 层的实现，不放配置头。 */

#endif /* APP_CONFIG_H */
