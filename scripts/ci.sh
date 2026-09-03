#!/usr/bin/env bash
# CI 状态查询工具（GitHub Actions）
# 用法:
#   ci.sh            列出最近的 CI runs
#   ci.sh jobs       最新 run 的 jobs 状态
#   ci.sh watch      等待最新 run 完成并输出结果
#   ci.sh <run-id>   指定 run 的 jobs 状态
set -euo pipefail

# 常见 gh 安装位置
export PATH="/opt/homebrew/bin:$PATH:/usr/local/bin:$PATH"

if ! command -v gh >/dev/null 2>&1; then
    echo "需要 GitHub CLI (gh)。安装: brew install gh" >&2
    exit 1
fi

latest_run() {
    gh run list --limit 1 --json databaseId -q '.[0].databaseId' 2>/dev/null \
        || { echo "无法获取最新 run（网络或认证问题）" >&2; exit 1; }
}

show_jobs() {
    local rid="$1"
    echo "run: $rid"
    gh run view "$rid" 2>/dev/null | sed -n '/JOBS/,/ANNOTATIONS/p'
}

case "${1:-list}" in
    list)
        gh run list --limit 6
        ;;
    jobs)
        show_jobs "$(latest_run)"
        ;;
    watch)
        rid="$(latest_run)"
        echo "等待 run $rid 完成..."
        if gh run watch "$rid" --exit-status >/dev/null 2>&1; then
            echo "✅ run $rid 成功"
        else
            echo "⚠ run $rid 失败或有问题" >&2
        fi
        show_jobs "$rid"
        ;;
    *)
        show_jobs "$1"
        ;;
esac
