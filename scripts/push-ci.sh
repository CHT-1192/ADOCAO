#!/usr/bin/env bash
# push-ci.sh — push 当前提交并跟踪 CI（gh run watch）
#
# 用法（commit 由你自己手动做）:
#   push-ci.sh [git push 参数…]     push 当前 HEAD → 跟踪 CI（默认）
#   push-ci.sh --watch              不 push：只看/等当前分支最新 CI run（默认输出详细信息）
#   push-ci.sh --stats              与 push 模式组合：结束时列各平台 job 耗时；成功时再列产物大小
#
# 特性:
#   - push 前打印本地领先/落后状态；push 失败自动重试（上限 3 次，间隔 5s）
#   - CI 全部成功 → 一句话确认；有失败 → 列失败 job（含平台 labels）+ 失败日志片段
set -euo pipefail

export PATH="/opt/homebrew/bin:$PATH:/usr/local/bin:$PATH"

if ! command -v gh >/dev/null 2>&1; then
    echo "需要 GitHub CLI (gh)。安装: brew install gh" >&2
    exit 1
fi

# ── 参数解析 ───────────────────────────────────────────────────
STATS=false
WATCH=false
PUSH_ARGS=()

while [ $# -gt 0 ]; do
    case "$1" in
        --stats) STATS=true; shift ;;
        --watch) WATCH=true; shift ;;
        *) PUSH_ARGS+=("$1"); shift ;;
    esac
done

# watch 模式默认输出详细信息（各平台耗时 + 产物）
[ "$WATCH" = true ] && STATS=true

# ── push（失败重试，上限 3 次）──────────────────────────────────
if [ "$WATCH" = false ]; then
    echo "▸ push 前状态: $(git status -sb | head -1)"
    echo "▸ git push ${PUSH_ARGS[*]:-}"
    PUSHED=false
    for i in 1 2 3; do
        if git push ${PUSH_ARGS[@]+"${PUSH_ARGS[@]}"}; then
            PUSHED=true
            break
        fi
        echo "⚠ push 失败（第 $i/3 次），5 秒后重试..." >&2
        [ "$i" -lt 3 ] && sleep 5
    done
    if [ "$PUSHED" = false ]; then
        echo "❌ push 连续失败 3 次，中止（不进入 CI 跟踪）" >&2
        exit 1
    fi
    echo "✅ push 成功（已同步 origin），开始跟踪 CI..."
else
    echo "▸ watch 模式：不 push，只看 CI"
fi

BRANCH="$(git branch --show-current 2>/dev/null || echo master)"
OWNER_REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null || true)"

# ── 取本次 push 对应的 run ────────────────────────────────────
# push 后新 run 注册可能滞后（旧 run 仍在"最新"位置）：push 模式按 headSha 匹配刚推的 commit；
# watch 模式（无 push）取当前分支最新 run。最多等 ~90s。
HEAD_SHA="$(git rev-parse HEAD 2>/dev/null || true)"
rid=""
for _ in $(seq 1 18); do
    if [ "$WATCH" = true ] || [ -z "$HEAD_SHA" ]; then
        rid="$(gh run list --branch "$BRANCH" --limit 1 --json databaseId -q '.[0].databaseId' 2>/dev/null || true)"
    else
        rid="$(gh run list --branch "$BRANCH" --limit 10 --json databaseId,headSha \
            -q ".[] | select(.headSha == \"$HEAD_SHA\") | .databaseId" 2>/dev/null | head -1 || true)"
    fi
    [ -n "$rid" ] && break
    sleep 5
done
if [ -z "$rid" ]; then
    echo "⚠ 未找到 workflow run（分支 '$BRANCH' 上的 workflow 是否触发？）" >&2
    exit 1
fi

title="$(gh run view "$rid" --json displayTitle -q .displayTitle 2>/dev/null || echo '')"
echo "▸ 跟踪 run #${rid}: ${title:-${rid}}"

# ── 统计输出（--stats）─────────────────────────────────────────
human_size() { # bytes → B/KB/MB
    local b="$1"
    if   [ "$b" -ge 1048576 ]; then awk -v n="$b" 'BEGIN{printf "%.1f MB", n/1048576}'
    elif [ "$b" -ge 1024 ];     then awk -v n="$b" 'BEGIN{printf "%.1f KB", n/1024}'
    else echo "${b} B"; fi
}

iso_epoch() { # ISO8601 → unix 秒（兼容 macOS/BSD 与 GNU date）
    local t="$1"
    t="${t%Z}"
    t="${t%.*}"
    if date -j -u -f "%Y-%m-%dT%H:%M:%S" "$t" +%s >/dev/null 2>&1; then
        date -j -u -f "%Y-%m-%dT%H:%M:%S" "$t" +%s
    else
        date -u -d "$1" +%s
    fi
}

fmt_dur() { # 秒 → XmYYs
    printf "%dm%02ds" "$(( $1 / 60 ))" "$(( $1 % 60 ))"
}

show_stats() {
    echo ""
    echo "── 各平台 job 耗时 ──"
    gh api "repos/$OWNER_REPO/actions/runs/$rid/jobs" \
        -q '.jobs[] | "\(.name) [\(.labels | join("/"))]\t\(.started_at)\t\(.completed_at)"' 2>/dev/null \
        | while IFS=$'\t' read -r name start end; do
              s="$(iso_epoch "$start" 2>/dev/null || echo 0)"
              e="$(iso_epoch "$end"   2>/dev/null || echo 0)"
              if [ "$s" -gt 0 ] && [ "$e" -ge "$s" ]; then
                  echo "  ⏱  $name  $(fmt_dur $(( e - s )))"
              else
                  echo "  ⏱  $name  (时间缺失)"
              fi
          done || true
    echo "── 产物 ──"
    gh api "repos/$OWNER_REPO/actions/runs/$rid/artifacts" \
        -q '.artifacts[] | "\(.name)\t\(.size_in_bytes)"' 2>/dev/null \
        | while IFS=$'\t' read -r name size; do
              echo "  📦 $name  ($(human_size "$size"))"
          done || true
}

# ── watch 并处理结果 ───────────────────────────────────────────
# 交互终端：显示 gh 的实时进度；非交互（日志/脚本环境）：静默等待，避免刷屏
WATCH_RC=0
if [ -t 1 ]; then
    gh run watch --exit-status "$rid" || WATCH_RC=$?
else
    echo "▸ 非交互模式：静默等待 run #${rid} 结束…"
    gh run watch --exit-status "$rid" >/dev/null 2>&1 || WATCH_RC=$?
fi

if [ "$WATCH_RC" -eq 0 ]; then
    echo ""
    echo "✅ CI 全部成功（run #${rid}）"
    [ "$STATS" = true ] && show_stats
    exit 0
fi

# ── 失败分支：报平台 + 具体信息 ──
echo ""
echo "❌ CI 失败（run #${rid}: ${title:-${rid}}）"
echo "── 失败的 job ──"
if [ -n "$OWNER_REPO" ]; then
    gh api "repos/$OWNER_REPO/actions/runs/$rid/jobs" \
        -q '.jobs[] | select(.conclusion=="failure" or .conclusion=="cancelled") |
             "  ✗ \(.name)   [平台: \(.labels | join(" / "))]"' 2>/dev/null || true
else
    gh run view "$rid" --json jobs \
        -q '.jobs[] | select(.conclusion=="failure" or .conclusion=="cancelled") | "  ✗ \(.name)"' 2>/dev/null || true
fi
echo "── 失败日志（末尾 100 行）──"
gh run view --log-failed "$rid" 2>/dev/null | tail -100 || true
echo ""
echo "完整信息: gh run view ${rid}  （网页: https://github.com/${OWNER_REPO}/actions/runs/${rid} ）"
exit 1
