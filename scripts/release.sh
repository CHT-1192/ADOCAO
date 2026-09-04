#!/usr/bin/env bash
# 发版: bump 版本号 + commit + tag + push（触发 Release workflow）
# 用法: release.sh <版本号>
#   稳定版:   release.sh 6.0.0
#   预发布版: release.sh 6.0.0-Beta1 / 6.0.0-Alpha2 / 6.0.0-Rc1
#   注意: CMake 的 project(VERSION) 只接受纯数字 —— CMakeLists 写入去掉后缀的核心号(6.0.0)，
#         git tag 用完整版本号(v6.0.0-Beta1)。
set -euo pipefail

V="${1:-}"
if ! [[ "$V" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-(Alpha|Beta|Rc)[0-9]*)?$ ]]; then
    echo "用法: release.sh <版本号>（x.y.z 或 x.y.z-AlphaN / -BetaN / -RcN）" >&2
    exit 1
fi
CORE="${V%-*}"   # 去掉 -AlphaN/-BetaN/-RcN 后缀 → 纯数字核心（写进 CMakeLists 用）

if ! grep -q "project(ADOCAO VERSION" CMakeLists.txt; then
    echo "找不到 CMakeLists.txt 版本行" >&2
    exit 1
fi

# macOS 与 Linux 的 sed -i 语法不同
if [[ "$(uname)" == "Darwin" ]]; then
    sed -i '' -E "s/project\(ADOCAO VERSION [0-9.]+/project(ADOCAO VERSION $CORE/" CMakeLists.txt
else
    sed -i -E "s/project\(ADOCAO VERSION [0-9.]+/project(ADOCAO VERSION $CORE/" CMakeLists.txt
fi

git add CMakeLists.txt
if git diff --cached --quiet; then
    echo "CMakeLists.txt 版本未变化（$CORE 已是最新），跳过 commit/push master"
else
    git commit -m "chore: bump version to $V"
    git push origin master
fi
git tag "v$V"
git push origin "v$V"

echo "✅ v$V 已推送（CMakeLists 版本: $CORE），Release workflow 自动构建三平台。跟踪: scripts/push-ci.sh --watch"
