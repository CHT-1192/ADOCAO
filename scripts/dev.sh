#!/usr/bin/env bash
# 增量构建 ADOCAO（开发用）
# 用法: dev.sh [cmake --build 额外参数]
set -euo pipefail

export PATH="/opt/homebrew/bin:$PATH:/usr/local/bin:$PATH"

if [ ! -d build ]; then
    echo "首次构建，先配置 CMake..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
fi

cmake --build build --parallel "$@"
echo "✅ 构建完成: build/$(ls build/ADOCAO-* 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo 'ADOCAO')"
