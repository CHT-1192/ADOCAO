#!/usr/bin/env bash
# 本地运行 ADOCAO（macOS / Linux）
# 用法: run.sh [参数...]
#   示例: run.sh                    启动 launcher
#         run.sh --debug            调试模式
#         run.sh --level x.adofai --music x.ogg  直接播放关卡
set -euo pipefail

BIN=""
for cand in ./build/ADOCAO-macOS ./build/ADOCAO-Linux ./build/ADOCAO ./build/adocao; do
    if [ -x "$cand" ]; then BIN="$cand"; break; fi
done

if [ -z "$BIN" ]; then
    echo "未找到构建产物，请先构建: cmake --build build --parallel" >&2
    exit 1
fi

exec "$BIN" "$@"
