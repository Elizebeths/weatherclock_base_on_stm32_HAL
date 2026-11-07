/**
 ******************************************************************************
 * @file    board.h
 * @brief   板级引脚与外设映射 —— 全工程唯一的引脚定义处
 *
 * @note    分层位置：Config/（编译期配置）。被 L0 的 Ports/ 与后续 L1 引用，
 *          本身不接触任何 HAL 符号（纯中性描述：bank + pin 序号）。
 ******************************************************************************
 *
 * 适配来源（按《智能天气时钟 · 项目实施计划》的适配优先级）
 *   一级 · 大型开源：ST 官方 STM32CubeF1（RM0008 的 DMA1 请求映射表）
 *   二级 · 自有模块：lcdLib-of-240x320-SPI-TFT-ST7789 的 lcd.h「用户配置区」
 *                    （LCD_xx_GPIO_PORT / LCD_xx_GPIO_PIN / LCD_SPI_HANDLE /
 *                      LCD_BL_ACTIVE_HIGH / LCD_WIDTH / LCD_HEIGHT / 字库几何）
 *                    与 iic_DHT20 的 iic_DHT20.ioc（PB6/PB7 = I2C1）
 *   三级 · 小型开源：未使用
 *
 * 与旧驱动的关系：旧 lcd.h 把「引脚标签宏 + 分辨率 + 字库几何 + 颜色 + API」
 * 混在一个公开头文件里，并 #include "main.h"（公开头文件即耦合 HAL）。
 * 本文件把那批配置宏收敛到这里，且**不再引用 CubeMX 生成的 TFT_xx_Pin 宏**，
 * 而是用 BOARD_PIN(bank, pin) 表达中性引脚，使 Ports/ 的头文件保持 HAL 无关。
 *
 * ============================ 引脚迁移说明（重要） ============================
 * 本工程把网络通道放在 **USART2（PA2=TX / PA3=RX）**，而旧 LCD 工程把
 * **TFT_RST = PA2、TFT_DC = PA3**，二者冲突。STM32F103C8T6 为 LQFP48 封装，
 * PD2~PD7 未引出，USART2 无法重映射到 PD5/PD6，因此只能迁移 TFT 引脚。
 * 为把改动面压到最小，只迁移 DC 与 RST：
 *
 *        信号       旧引脚    新引脚    说明
 *        TFT_BL     PA1      PA1     保留；PA1 同时是 TIM2_CH2，为后续背光调光预留
 *        TFT_RST    PA2  →   PB1     迁移（PA2 让给 USART2_TX）
 *        TFT_DC     PA3  →   PB0     迁移（PA3 让给 USART2_RX）
 *        TFT_CS     PA4      PA4     保留；SPI1_NSS 作普通 GPIO 软件片选
 *        SPI1_SCK   PA5      PA5     保留
 *        SPI1_MOSI  PA7      PA70     保留
 *
 * ⚠ 硬件需同步改线：把 TFT 模块的 RST 从 PA2 改接到 PB1、DC 从 PA3 改接到 PB0。
 *
 * ========================== CubeMX 配置清单（人工执行） ==========================
 * 目标：在 weatherclock.ioc 中补齐下列外设并重新生成代码（本工程不代改 .ioc）。
 *
 * (0) System Core - SYS：Debug = Serial Wire；RCC：HSE = Crystal/Ceramic Resonator
 *     （工程已具备；SYSCLK = 72 MHz，APB1 = 36 MHz，APB2 = 72 MHz）
 *
 * (1) DMA 控制器（启用任意外设 DMA 后 CubeMX 会自动生成 Core/Src/dma.c 的
 *     MX_DMA_Init()，它负责使能 DMA1 时钟与 NVIC —— **必须在 MX_SPI1_Init()
 *     与 MX_USART2_UART_Init() 之前调用**）
 *
 * (2) Connectivity - SPI1（TFT）
 *       Mode                : Transmit Only Master（与旧驱动一致，省一根 MISO）
 *       Frame Format        : Motorola
 *       Data Size           : 8 Bits
 *       First Bit           : MSB First
 *       Prescaler           : 2      → 72/2 = 36 MHz（性能目标档）
 *                             若 36 MHz 不稳，回退 4（= 18 MHz），并记录实际稳定档
 *       Clock Polarity/Phase: Low / 1 Edge
 *       NSS                 : Software（Disable Hardware NSS）
 *       引脚自动分配        : PA5 = SPI1_SCK，PA7 = SPI1_MOSI
 *       DMA Settings        : Add → SPI1_TX → DMA1 Channel 3
 *                               Direction            : Memory To Peripheral
 *                               Priority             : High
 *                               Mode                 : Normal
 *                               Increment Address    : Memory ✔ / Peripheral ✘
 *                               Data Width           : Byte / Byte
 *       NVIC Settings       : DMA1 channel3 global interrupt → Enabled
 *
 * (3) Connectivity - I2C1（AHT20/DHT20 与后续 CST816S 触摸共用）
 *       I2C                 : I2C（非 SMBus）
 *       Speed Mode          : Standard（100 kHz）；如走线良好可改 Fast Mode
 *       引脚自动分配        : PB6 = I2C1_SCL，PB7 = I2C1_SDA
 *
 * (4) Connectivity - USART2（网络通道，PH2 预留）
 *       Mode                : Asynchronous
 *       Baud Rate           : 115200，8 Bits，No Parity，1 Stop
 *       引脚自动分配        : PA2 = USART2_TX，PA3 = USART2_RX
 *       DMA Settings        : Add → USART2_TX → DMA1 Channel 7（Normal / Byte / Byte）
 *                             Add → USART2_RX → DMA1 Channel 6（Normal / Byte / Byte）
 *       NVIC Settings       : USART2 global interrupt          → Enabled
 *                             DMA1 channel6 global interrupt   → Enabled
 *                             DMA1 channel7 global interrupt   → Enabled
 *
 * (5) Connectivity - USART1（分级日志，PH1 系统基础使用）
 *       Mode                : Asynchronous
 *       Baud Rate           : 115200，8 Bits，No Parity，1 Stop
 *       引脚自动分配        : PA9 = USART1_TX，PA10 = USART1_RX
 *       NVIC Settings       : 按需（当前只做发送，可不使能 RX 中断）
 *
 * (6) 引脚配置（在芯片图上左键点引脚 → GPIO_Output，右键 → Enter User Label）
 *       PA1 → TFT_BL    Output Push Pull / High / Speed High / No pull
 *       PA4 → TFT_CS    Output Push Pull / High / Speed High / Pull-up
 *       PB0 → TFT_DC    Output Push Pull / High / Speed High / No pull   ← 迁移
 *       PB1 → TFT_RST   Output Push Pull / High / Speed High / No pull   ← 迁移
 *     User Label 仅用于人工核对与 CubeMX 生成引脚宏；L0 不依赖这些标签，
 *     引脚号以本文件 BOARD_xx_PIN 为唯一事实来源。
 *
 * (7) 后续 PH4 输入设备若使用 EXTI：在 CubeMX 中把按键/触摸 INT 引脚配成
 *     GPIO_EXTI 并在 NVIC 中使能，CubeMX 会生成对应的 EXTIx_IRQHandler
 *     （它内部调用 HAL_GPIO_EXTI_IRQHandler），Port 层只做「运行时改边沿 +
 *     注册回调」，因此无需改动任何 CubeMX 生成文件。
 *
 * (8) Project Manager → Code Generator：
 *       勾选 "Copy only the necessary library files"
 *       勾选 "Generate peripheral initialization as a pair of .c/.h files"
 *     重新生成后，CubeMX 会自动补入 stm32f1xx_hal_spi.c/.h、stm32f1xx_hal_i2c.c/.h、
 *     stm32f1xx_hal_uart.c/.h、stm32f1xx_hal_uart_ex.c/.h，并在
 *     Core/Inc/stm32f1xx_hal_conf.h 中打开 HAL_SPI/I2C/UART_MODULE_ENABLED。
 *     ⚠ 当前工程的 HAL 为 "Copy only necessary library files" 模式且**不含 UART HAL**，
 *       在完成本清单第 (4)(5) 步之前，Ports/bus/uart_port_stm32.c 无法编译。
 *
 * (9) STM32CubeIDE 侧：把 Ports/、Config/、Devices/、App/ 登记为源文件夹与**四根**包含路径
 *       Project → Properties → C/C++ General → Paths and Symbols
 *         Source Location : Add Folder → Ports、Config、Devices、App
 *         Includes        : Add → Ports、Config、Devices、App
 *     说明：跨目录包含统一写成「以所在层根为根」的形式：
 *         L0（Ports）  → 「以 Ports 为根」   如 #include "common/port.h"
 *         L1（Devices）→ 「以 Devices 为根」 如 #include "display/lcd.h"、
 *                                             #include "display/panel/lcd_panel.h"
 *         L3（App）    → 「以 App 为根」     如 #include "display/app_display.h"
 *     因此**不需要**为 common/gpio/display/bus、display/panel 等子目录分别登记；
 *     子目录增减时也无需改动工程设置。
 *     本工程已把上述四个源文件夹与包含路径写入 .cproject（Debug/Release 两配置）。
 *     （CubeMX 重新生成不会破坏手工登记的 Source Location，但每次大改 .ioc 后建议复查）
 *
 * ============================== 引脚占用与预留 ==============================
 *   已占用：PA1/PA4（TFT）、PA2/PA3（USART2）、PA5/PA7（SPI1）、PA9/PA10（USART1）、
 *           PA13/PA14（SWD）、PB0/PB1（TFT）、PB6/PB7（I2C1）、PD0/PD1（HSE）
 *   预留  ：PA0          → PH3 STOP 模式 WKUP
 *           PC14/PC15    → PH3 LSE 32.768 kHz（RTC）
 *           PA6          → SPI1_MISO（本轮 TX-Only 不使用）
 *           PA8/PA11/PA12/PA15/PB3/PB4/PB5/PB8~PB15 → PH4 输入设备（按键/编码器/触摸 INT）
 *           PA1 的 TIM2_CH2 复用 → 背光 PWM 调光
 *   禁止  ：PB2（BOOT1）
 ******************************************************************************
 */

#ifndef BOARD_H
#define BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* gpio_pin_t / gpio_bank_t 由 L0 的 gpio_port.h 提供（中性类型，无 HAL 依赖）。
 * 以 Ports/ 为根包含，使工程只需 -I Ports 与 -I Config 两个包含路径。 */
#include "gpio/gpio_port.h"

/* ==========================================================================
 * 一、外设实例选择（中性：只表达「第几号」，由 *_stm32.c 映射到 HAL 符号）
 * ========================================================================== */

#define BOARD_SPI_LCD_INDEX 1U /**< 显示总线使用 SPI1 */
#define BOARD_I2C_BUS_INDEX 1U /**< 传感器/触摸总线使用 I2C1（**可被两模块共用**） */
#define BOARD_UART_NET_INDEX 2U /**< 网络通道使用 USART2（PH2，当前无设备使用） */
#define BOARD_UART_LOG_INDEX 1U /**< 分级日志使用 USART1 */

/* ==========================================================================
 * 二、DMA 通道号（中性：DMA1 的通道序号 1..7）
 * 依据 ST 官方 RM0008《STM32F103xx 参考手册》「DMA1 request mapping」：
 *   SPI1_TX  → DMA1 Channel 3
 *   USART2_TX→ DMA1 Channel 7
 *   USART2_RX→ DMA1 Channel 6
 * ========================================================================== */

#define BOARD_SPI_LCD_TX_DMA_CHANNEL 3U /**< RM0008: SPI1_TX → DMA1 Ch3 */
#define BOARD_UART_NET_TX_DMA_CHANNEL 7U /**< RM0008: USART2_TX → DMA1 Ch7 */
#define BOARD_UART_NET_RX_DMA_CHANNEL 6U /**< RM0008: USART2_RX → DMA1 Ch6 */

/* ==========================================================================
 * 三、引脚定义（唯一事实来源）
 * 每个信号给出三种形式：
 *   BOARD_xxx_BANK / BOARD_xxx_PIN_INDEX —— 常量整数，可放进 static const 数组
 *   BOARD_xxx_PIN                         —— 中性 gpio_pin_t 复合字面量（C99），
 *                                            可直接作函数实参
 *   gpio_port_write(BOARD_TFT_CS_PIN, false);
 * ========================================================================== */

#define BOARD_PIN(bank_, pin_)                                                 \
  ((gpio_pin_t){(uint8_t)(bank_), (uint8_t)(pin_)})

/** 由「bank 常量 + 序号常量」拼出中性引脚（Port 层内部建映射表用） */
#define BOARD_PIN_OF(bank_, pin_index_)                                        \
  ((gpio_pin_t){(uint8_t)(bank_), (uint8_t)(pin_index_)})

/* --- 显示（SPI1 + 软件片选） --- */
#define BOARD_TFT_BL_BANK      GPIO_BANK_A /**< 原 PA1；兼 TIM2_CH2 可作调光 */
#define BOARD_TFT_BL_PIN_INDEX 1U
#define BOARD_TFT_CS_BANK      GPIO_BANK_A /**< 原 PA4；SPI1_NSS 作普通 GPIO */
#define BOARD_TFT_CS_PIN_INDEX 4U
#define BOARD_TFT_DC_BANK      GPIO_BANK_B /**< 迁移：原 PA3 */
#define BOARD_TFT_DC_PIN_INDEX 0U
#define BOARD_TFT_RST_BANK     GPIO_BANK_B /**< 迁移：原 PA2 */
#define BOARD_TFT_RST_PIN_INDEX 1U

#define BOARD_TFT_BL_PIN                                                       \
  BOARD_PIN(BOARD_TFT_BL_BANK, BOARD_TFT_BL_PIN_INDEX)
#define BOARD_TFT_CS_PIN                                                       \
  BOARD_PIN(BOARD_TFT_CS_BANK, BOARD_TFT_CS_PIN_INDEX)
#define BOARD_TFT_DC_PIN                                                       \
  BOARD_PIN(BOARD_TFT_DC_BANK, BOARD_TFT_DC_PIN_INDEX)
#define BOARD_TFT_RST_PIN                                                      \
  BOARD_PIN(BOARD_TFT_RST_BANK, BOARD_TFT_RST_PIN_INDEX)

/** 背光有效电平：1 = 高电平点亮（与旧 lcd.h 的 LCD_BL_ACTIVE_HIGH 一致） */
#define BOARD_TFT_BL_ACTIVE_HIGH 1

/* 显示几何（屏幕尺寸、面板显存偏移、条带/填充缓冲、脏区表容量）已迁至
 * Config/display_cfg.h ——「显示配置的唯一事实来源」在那里，本文件只保留
 * 引脚与外设映射，避免把显示几何与引脚定义混在一处。
 * 这样 Devices/display 的公共类型层只需包含 display_cfg.h（零依赖），
 * 不再经本文件被拖入 gpio_port.h 的引脚类型。 */

/* --- 传感器 / 触摸（共用 I2C1） --- */
#define BOARD_I2C_SCL_PIN BOARD_PIN(GPIO_BANK_B, 6U) /**< I2C1_SCL */
#define BOARD_I2C_SDA_PIN BOARD_PIN(GPIO_BANK_B, 7U) /**< I2C1_SDA */

/** DHT20/AHT20 的 7 位从机地址（HAL 需要 8 位左移形式，由 i2c_port 内部左移） */
#define BOARD_AHT20_I2C_ADDR7 0x38U

/* --- 网络通道（USART2，当前无设备使用） --- */
#define BOARD_NET_UART_TX_PIN BOARD_PIN(GPIO_BANK_A, 2U)
#define BOARD_NET_UART_RX_PIN BOARD_PIN(GPIO_BANK_A, 3U)

/* --- 分级日志（USART1） --- */
#define BOARD_LOG_UART_TX_PIN BOARD_PIN(GPIO_BANK_A, 9U)
#define BOARD_LOG_UART_RX_PIN BOARD_PIN(GPIO_BANK_A, 10U)

/**
 * 是否启用日志通道（USART1）。
 * 若 CubeMX 中未启用 USART1（只为省 Flash 而放弃日志），请把此宏置 0，
 * 这样 uart_port_stm32.c 不会引用不存在的 huart1、也能顺利编译。
 */
#define BOARD_UART_LOG_ENABLE 1

/* --- 预留位（本阶段不使用，仅登记以免被误占） --- */
#define BOARD_WKUP_PIN    BOARD_PIN(GPIO_BANK_A, 0U)   /**< PH3 STOP 唤醒 */
#define BOARD_BOOT1_PIN   BOARD_PIN(GPIO_BANK_B, 2U)   /**< BOOT1，禁止作他用 */
#define BOARD_SPI_LCD_MISO_PIN BOARD_PIN(GPIO_BANK_A, 6U) /**< 本轮 TX-Only 不使用 */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
