#!/usr/bin/env bash
# 发版: bump 版本号 + commit + tag + push（触发 Release workflow）
# 用法: release.sh <版本号>   例如: release.sh 5.0.1
set -euo pipefail

V="${1:-}"
if ! [[ "$V" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "用法: release.sh <版本号>（格式 x.y.z）" >&2
    exit 1
fi

if ! grep -q "project(ADOCAO VERSION" CMakeLists.txt; then
    echo "找不到 CMakeLists.txt 版本行" >&2
    exit 1
fi

# macOS 与 Linux 的 sed -i 语法不同
if [[ "$(uname)" == "Darwin" ]]; then
    sed -i '' -E "s/project\(ADOCAO VERSION [0-9.]+/project(ADOCAO VERSION $V/" CMakeLists.txt
else
    sed -i -E "s/project\(ADOCAO VERSION [0-9.]+/project(ADOCAO VERSION $V/" CMakeLists.txt
fi

git add CMakeLists.txt
git commit -m "chore: bump version to $V"
git push origin master
git tag "v$V"
git push origin "v$V"

echo "✅ v$V 已推送，Release workflow 自动构建三平台。查状态: scripts/ci.sh watch"
