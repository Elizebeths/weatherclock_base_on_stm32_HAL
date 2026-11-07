# STM32 HAL 智能天气时钟

基于 **STM32F103C8T6** 和 STM32 HAL 的天气时钟项目，围绕 ST7789 彩屏、本地温湿度采集、串口 AT 网络通信与网络时间解析构建。驱动采用分层设计，将设备逻辑与 STM32 外设适配分开，便于独立调试和移植。

> **当前仓库状态**：已包含外设初始化、设备驱动、适配层和 CubeMX 配置，但尚未包含 `main.c` 所依赖的 `App/` 应用层，以及 STM32CubeIDE 的 `.project` / `.cproject` 工程文件。因此，当前版本不能仅靠克隆仓库直接编译出完整天气时钟；需要补齐应用层并重建工程配置。下文按实际提交内容说明。

## 已包含的模块

| 模块 | 内容 |
| --- | --- |
| 显示 | ST7789 面板驱动、RGB565 绘图、ASCII 字体、文本渲染、脏矩形与条带刷新、SPI DMA 传输 |
| 本地温湿度 | DHT20/AHT20 驱动、设备探测、转换等待与超时处理、CRC8 校验 |
| 网络 | AT 指令交互、Station 模式配置、Wi-Fi 连接、PING、TCP 连接与 HTTP 请求 |
| 天气数据 | 从 HTTP 响应正文解析温度与湿度；默认请求城市为 Wuhan |
| 网络时间 | 从 HTTP `Date` 响应头解析 GMT 时间，转换为北京时间（UTC+8），处理日期进位 |
| 日志 | EasyLogger，通过 USART1 输出分级日志 |
| 移植接口 | GPIO、SPI、I2C、UART、临界区与 OS 适配接口 |

网络时间接口返回最近一次成功解析的时间快照，**不会自行走时，也不检查快照是否过期**。持续时钟显示与业务调度需要应用层完成；当前 `.ioc` 未配置 RTC。

## 硬件与连接

- 主控：STM32F103C8T6，Cortex-M3；链接脚本按 64 KB Flash / 20 KB SRAM 配置。
- 系统时钟：HSE 经 PLL 倍频至 72 MHz。
- 显示屏：240 × 320 SPI TFT，ST7789 控制器，RGB565。
- 传感器：DHT20/AHT20，I2C 7 位地址 `0x38`。
- 网络模块：支持源码所用 AT 指令的串口 Wi-Fi 模块；连接前需核对模块固件的指令兼容性。
- 调试：SWD 调试器；串口日志可使用 USB 转串口模块查看。

以下连接来自 `Config/board.h` 与 `Core/Src/` 的外设初始化代码：

| 模块 | 信号 | STM32 引脚 | 说明 |
| --- | --- | --- | --- |
| TFT | SCK | PA5 | SPI1 时钟 |
| TFT | MOSI / SDA | PA7 | SPI1 数据输出 |
| TFT | CS | PA4 | 软件片选 |
| TFT | DC | PB0 | 数据 / 命令选择 |
| TFT | RST | PB1 | 屏幕复位 |
| TFT | BL | PA1 | 高电平点亮背光 |
| DHT20/AHT20 | SCL | PB6 | I2C1，100 kHz |
| DHT20/AHT20 | SDA | PB7 | I2C1 数据 |
| Wi-Fi 模块 | RX | PA2 | USART2_TX → 模块 RX |
| Wi-Fi 模块 | TX | PA3 | USART2_RX ← 模块 TX |
| 日志串口 | USB 转串口 RX | PA9 | USART1_TX |
| 日志串口 | USB 转串口 TX | PA10 | USART1_RX；仅查看日志时可不接 |
| SWD | SWDIO | PA13 | 下载 / 调试 |
| SWD | SWCLK | PA14 | 下载 / 调试 |

各模块需共地；电源按具体模块规格接入，串口使用与 STM32 兼容的 3.3 V 逻辑电平。I2C 总线需有合适的上拉。

**接线差异**：本工程的 TFT `DC` / `RST` 分别使用 **PB0 / PB1**。旧接线若使用 PA3 / PA2，需要迁移，否则会与 USART2 网络通道冲突。

两路串口均配置为 **115200、8N1、无硬件流控**。SPI1 当前配置为 Mode 0、36 MHz；若屏幕通信不稳定，可将分频由 2 调为 4（18 MHz），并同步修改 `.ioc` 和相关配置记录。

## 目录结构

```text
.
├── Config/                        # 板级引脚、功能开关、显示参数
├── Core/
│   ├── Inc/                       # HAL 配置与外设头文件
│   ├── Src/                       # main、外设初始化、中断入口
│   └── Startup/                   # 启动文件
├── Devices/
│   ├── display/                   # 面板、画布、绘图、文本与刷新
│   ├── net/                       # AT 通信、天气与时间解析
│   └── sensor/                    # DHT20/AHT20 与传感器抽象
├── Drivers/                       # CMSIS 与 STM32F1 HAL
├── Middlewares/EasyLogger/         # 日志库与移植代码
├── Ports/                         # STM32 总线、GPIO 与 OS 适配
├── STM32F103C8TX_FLASH.ld          # Flash / RAM 链接布局
├── weatherclock.ioc               # CubeMX 配置
├── weatherclock Debug.launch      # 调试启动配置，使用前核对本机路径
└── LICENSE
```

`Core/Src/main.c` 负责 HAL、时钟、外设和日志初始化，随后调用 `app_init()` 与 `app_loop()`。它包含的 `app_main.h` 及对应实现目前未随仓库提交。

## 获取与重建工程

```bash
git clone https://github.com/Elizebeths/weatherclock_base_on_stm32_HAL.git
cd weatherclock_base_on_stm32_HAL
```

配置文件记录的开发环境为：

| 项目 | 记录值 |
| --- | --- |
| CubeMX | 6.18.0 |
| STM32 固件包 | STM32Cube FW_F1 V1.8.7 |
| 目标工具链 | STM32CubeIDE / GCC |
| 链接脚本 | `STM32F103C8TX_FLASH.ld` |

这些值来自项目配置，当前提交未进行完整构建或上板验证。

1. 先备份自定义代码，再通过 CubeMX / STM32CubeIDE 打开 `weatherclock.ioc`，生成本机工程配置。重新生成后核对 `main.c` 的用户代码、外设初始化顺序与中断回调。
2. 补齐 `App/` 应用层，提供 `app_main.h`、`app_init()` 和 `app_loop()`，完成显示内容、传感器采样、联网请求与时间更新的调度。仅重新生成 CubeMX 代码不会补齐这些业务实现。
3. 在工程中登记 `Ports/`、`Devices/`、`Middlewares/EasyLogger/src/`、`Middlewares/EasyLogger/port/` 及补齐后的 `App/` 源文件。添加包含路径：`Config/`、`Ports/`、`Devices/`、`Middlewares/EasyLogger/inc/`、`App/`，并保留生成的 Core / HAL / CMSIS 包含路径。
4. 核对 SPI1_TX 使用 DMA1 Channel 3，USART2_RX / TX 分别使用 DMA1 Channel 6 / 7；确保 DMA 初始化先于相关外设初始化，并保留所需中断与 Port 层回调衔接。
5. 按下节配置 Wi-Fi 与请求城市，核对链接脚本后构建，再通过 SWD 下载。
6. 打开 USART1 日志串口（115200 8N1）。当前 `main.c` 在进入应用初始化前会输出各级别的 `Hello EasyLogger!`，可用于检查日志链路。

`Debug/`、`Release/` 和 `.DS_Store` 已被忽略。历史构建产物不随仓库提供，不能作为当前源码可直接构建的依据。

## 常用配置

| 位置 | 配置内容 |
| --- | --- |
| `Config/board.h` | 板级引脚、外设索引、DMA 通道、背光有效电平 |
| `Config/display_cfg.h` | 分辨率、面板偏移、RGB565 格式、条带缓冲与脏区容量 |
| `Config/app_config.h` | 采样与刷新周期、总线超时、日志级别、自检与 OS 开关 |
| `Devices/net/Device_net.c` | `device_net_CWJAP()` 中的 Wi-Fi 连接参数 |
| `Devices/net/Device_net.h` | `request` HTTP 请求字符串与城市路径 |
| `weatherclock.ioc` | CubeMX 外设与时钟配置 |

默认天气请求访问 `wttr.in`，路径为 `/Wuhan?format=%25t%7C%25h&m`，请求温度与湿度并用 `|` 分隔。更换城市时修改请求路径，同时保持主机与 TCP 连接目标一致。请求使用明文 HTTP，结果与时间解析依赖服务可达性及响应格式。

主要默认值：

- `APP_OS_FREERTOS = 0`：裸机模式；刷新调度需要应用循环调用相应接口。启用 RTOS 前需补齐内核、中间件与调度器启动代码。
- `DISPLAY_STRIP_LINES = 2`：240 像素宽、RGB565 条带缓冲为 960 B，避免使用整屏帧缓冲。
- `APP_SENSOR_PERIOD_MS = 10000`，`APP_NET_PERIOD_MS = 30000`：供应用层使用的周期参数，实际执行取决于补齐后的调度代码。
- `APP_SENSOR_ENABLE_SELFTEST = 1`，`APP_LCD_ENABLE_SELFTEST = 0`：传感器与显示自检开关，需由对应应用代码调用。
- `APP_LOG_LEVEL_DEFAULT = APP_LOG_LEVEL_VERBOSE`：默认日志级别。

使用自己的设备时，请先检查并修改源码中的 Wi-Fi 连接参数；README 不重复列出仓库中的具体名称和密码。

## 检查与排查

可在仓库根目录运行现有分层检查脚本：

```bash
sh Ports/check_portability.sh
```

该脚本检查其扫描范围内的硬件符号依赖，不能替代编译、链接或硬件测试。

| 现象 | 优先检查 |
| --- | --- |
| 找不到 `app_main.h`，或无法链接 `app_init` / `app_loop` | 当前仓库缺少应用层，需先补齐 `App/` |
| IDE 无法直接导入为现成工程 | 当前未提交 `.project` / `.cproject`，需重建工程配置 |
| 屏幕不亮或花屏 | 供电、共地、BL 电平、PB0 / PB1 接线、SPI 速率与面板偏移 |
| 传感器无应答 | PB6 / PB7、I2C 上拉、供电与地址 `0x38` |
| AT 指令超时 | USART2 收发交叉接线、模块波特率、AT 固件兼容性、DMA / IDLE 中断 |
| 天气或时间解析失败 | 网络连接、HTTP 响应格式、正文分隔符与 `Date` 响应头 |
| 时间停留在上次更新值 | 时间接口仅提供快照，需由应用层负责持续走时与重新同步 |

## 许可证

仓库根目录提供 [GNU GPL v3 许可证文本](LICENSE)。CMSIS、STM32 HAL 等第三方组件保留各自目录中的许可证与源码声明，使用和分发时请一并查阅。
