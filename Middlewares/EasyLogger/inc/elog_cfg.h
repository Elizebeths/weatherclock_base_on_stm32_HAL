/*
 * This file is part of the EasyLogger Library.
 *
 * Copyright (c) 2015-2016, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: It is the configure head file for this library.
 *
 * ---------------------------------------------------------------------------
 * 本工程适配说明（weatherclock / STM32F103C8T6）
 *   - 静态日志级别直接取自 Config/app_config.h 的 APP_LOG_LEVEL_DEFAULT，
 *     让「日志级别」在全工程只有一个事实来源；
 *   - 关闭异步输出 / 缓冲输出（裸机无 RTOS 与文件系统），因此不依赖
 *     elog_async.c / elog_buf.c，也不需要 pthread；
 *   - 行缓冲下调到 256 字节（上游默认 1024，对 20 KB RAM 偏大）；
 *   - 与 demo（demo/non_os/stm32f10x）一致地不启用颜色与 func/dir/line 格式，
 *     以节省 Flash；需要时再打开对应宏即可。
 * ---------------------------------------------------------------------------
 */

#ifndef _ELOG_CFG_H_
#define _ELOG_CFG_H_

#include "app_config.h" /* 仅取 APP_LOG_LEVEL_* / APP_UART_TX_TIMEOUT_MS 等编译期配置 */

/* enable log output. Keep this macro defined to enable log output. */
#define ELOG_OUTPUT_ENABLE

/* setting static output log level. range: from ELOG_LVL_ASSERT to ELOG_LVL_VERBOSE.
 * APP_LOG_LEVEL_ERROR/WARN/INFO/DEBUG（1..4）与 ELOG_LVL_ERROR/WARN/INFO/DEBUG
 * 取值一致，故可安全直传；APP_LOG_LEVEL_OFF(0) 等价于仅保留断言输出。 */
#define ELOG_OUTPUT_LVL                          APP_LOG_LEVEL_DEFAULT

/* enable assert check */
#define ELOG_ASSERT_ENABLE

/* buffer size for every line's log */
#define ELOG_LINE_BUF_SIZE                       256

/* output line number max length */
#define ELOG_LINE_NUM_MAX_LEN                    5

/* output filter's tag max length */
#define ELOG_FILTER_TAG_MAX_LEN                  16

/* output filter's keyword max length */
#define ELOG_FILTER_KW_MAX_LEN                   16

/* output filter's tag level max num */
#define ELOG_FILTER_TAG_LVL_MAX_NUM              5

/* output newline sign */
#define ELOG_NEWLINE_SIGN                        "\r\n"

#endif /* _ELOG_CFG_H_ */
