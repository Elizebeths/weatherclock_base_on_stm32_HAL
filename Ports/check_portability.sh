#!/bin/sh
#
# check_portability.sh —— L0 分层与可移植性验证（对应 PH1 验收门 G1 / G4）
#
# 判据（两项全过才算通过）：
#   G1-a  公开头文件零硬件符号：Ports/**/*.h 与 Config/*.h 的**代码部分**
#         （注释与字符串已剥离）不得出现
#         HAL_ / __HAL_ / stm32 / hspi / hi2c / huart / HandleTypeDef /
#         GPIO_TypeDef / GPIOA..GPIOD / DWT / SPI1 / I2C1 / USARTx
#   G1-b  逻辑目录零硬件符号：Devices/ Services/ App/ 同样不得出现（目录不存在则跳过）
#
# 用法：  sh Ports/check_portability.sh
#
# 适配来源：一级·大型开源 —— 判据取自《PH1 显示攻坚专题》第 01 节末
#           「分层是否成立的唯一客观判据」，此处脚本化以便每次提交前回归。
#
# 说明：源码文件头部的「适配来源」注释里会正常出现 HAL 字样（那是文档，
#       不是耦合），因此必须先剥离注释再匹配，否则会产生大量假告警。

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

HW_SYMBOLS='HAL_|__HAL_|stm32|STM32|hspi|hi2c|huart|HandleTypeDef|GPIO_TypeDef|GPIOA|GPIOB|GPIOC|GPIOD|\\bDWT\\b|CoreDebug|\\bSPI1\\b|\\bI2C1\\b|\\bUSART[123]\\b'

status=0

# ---------------------------------------------------------------- 工具：剥离注释
# 逐行扫描，丢弃 /* */（可跨行）与 // 之后的内容；保留字符串字面量；
# 行数不增不减，因此 grep -n 报出的行号与原文一致。
strip_comments() {
    awk '
    BEGIN { inblock = 0 }
    {
        line = $0; out = ""; i = 1; n = length(line)
        while (i <= n) {
            two = substr(line, i, 2)
            if (inblock) {
                if (two == "*/") { inblock = 0; i += 2 } else { i++ }
                continue
            }
            if (two == "/*") { inblock = 1; i += 2; continue }
            if (two == "//") { break }
            c = substr(line, i, 1)
            if (c == "\"") {
                out = out c; i++
                while (i <= n) {
                    c = substr(line, i, 1); out = out c; i++
                    if (c == "\\") { if (i <= n) { out = out substr(line, i, 1); i++ } }
                    else if (c == "\"") break
                }
                continue
            }
            out = out c; i++
        }
        print out
    }' "$1"
}

# 在若干文件里查硬件符号；命中则打印 "文件:行号:内容"
scan_hw() {
    for f in "$@"; do
        [ -f "$f" ] || continue
        strip_comments "$f" | grep -nE "$HW_SYMBOLS" | while IFS= read -r hit; do
            printf '%s:%s\n' "$f" "$hit"
        done
    done
}

printf '=== G1-a 公开头文件零硬件符号 ===\n'
hits=$(scan_hw "$ROOT"/Ports/*/*.h "$ROOT"/Config/*.h)
if [ -n "$hits" ]; then
    printf '%s\n' "$hits"
    printf 'FAIL: 头文件出现硬件符号（应只出现在 *_stm32.c 中）\n'
    status=1
else
    printf 'PASS\n'
fi

printf '\n=== G1-b 逻辑目录（Devices/ Services/ App/）零硬件符号 ===\n'
found_any=0
for d in Devices Services App; do
    if [ -d "$ROOT/$d" ]; then
        found_any=1
        hits=$(scan_hw "$ROOT/$d"/*.h "$ROOT/$d"/*.c "$ROOT/$d"/*/*.h "$ROOT/$d"/*/*.c)
        if [ -n "$hits" ]; then
            printf '%s\n' "$hits"
            printf 'FAIL: %s/ 出现硬件符号\n' "$d"
            status=1
        fi
    fi
done
if [ "$found_any" -eq 1 ] && [ "$status" -eq 0 ]; then
    printf 'PASS\n'
elif [ "$found_any" -eq 0 ]; then
    printf 'SKIP: Devices/ Services/ App/ 尚未创建（L1/L2/L3 未落地）\n'
fi

printf '\n=== 结果 ===\n'
if [ "$status" -eq 0 ]; then
    printf 'OK: L0 分层与可移植性验证全部通过\n'
else
    printf 'FAILED\n'
fi
exit "$status"
