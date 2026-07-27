#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# ── Colour helpers ──────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; DIM='\033[2m';  NC='\033[0m'

# ── Compiler detection ──────────────────────────────────────────
detect_compiler() {
    if command -v g++ &>/dev/null; then
        CXX="$(command -v g++)"; CC="$(command -v gcc)"
        echo "G++ (GCC)"
    elif command -v clang++ &>/dev/null; then
        CXX="$(command -v clang++)"; CC="$(command -v clang)"
        echo "Clang++"
    else
        echo ""
    fi
}

# ── Prompt helper ───────────────────────────────────────────────
prompt_bool() {
    local label="$1" default="${2:-N}"
    local yn="y/N"
    [[ "$default" == "Y" ]] && yn="Y/n"
    read -r -p "  $label ($yn) " reply
    [[ -z "$reply" ]] && reply="$default"
    [[ "$reply" =~ ^[Yy] ]]
}

# ── Argument parsing ────────────────────────────────────────────
INTERACTIVE=true
PORTABLE=OFF; ZOOM_LEVEL="Ultra"

for arg in "$@"; do
    INTERACTIVE=false
    case "${arg}" in
        -Portable|-P)   PORTABLE=ON ;;
        -Normal|-N)         ZOOM_LEVEL="Normal" ;;
        -Extra|-T)          ZOOM_LEVEL="Extra" ;;
        -Super|-S)          ZOOM_LEVEL="Super" ;;
        -Ultra|-U)          ZOOM_LEVEL="Ultra" ;;
        -Hyper|-H)          ZOOM_LEVEL="Hyper" ;;
        -Extreme|-X)        ZOOM_LEVEL="Extreme" ;;
        -Unimaginable|-I)   ZOOM_LEVEL="Unimaginable" ;;
        *) echo "Unknown: $arg"; exit 1 ;;
    esac
done

# ── Banner ──────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}╔══════════════════════════════════════════╗"
echo -e "${CYAN}║     ADOCAO  v3.0.0  —  Build Script      ║"
echo -e "${CYAN}╚══════════════════════════════════════════╝"
echo ""

# ── Detect compiler ─────────────────────────────────────────────
CXX_KIND=$(detect_compiler)
if [[ -z "$CXX_KIND" ]]; then
    echo -e "${RED}ERROR: No compiler found (g++ or clang++).${NC}"
    exit 1
fi
echo -ne "Compiler detected: "; echo -e "${GREEN}$CXX_KIND${NC}"
echo -e "${DIM}  CC  = $CC${NC}"
echo -e "${DIM}  CXX = $CXX${NC}"

# ── Interactive prompts ─────────────────────────────────────────
if $INTERACTIVE; then
    echo ""
    echo -e "${YELLOW}Build options (press Enter for default):${NC}"
    prompt_bool "  Static-linked portable build?"  "N" && PORTABLE=ON
    echo "  Zoom level: Normal(10) Extra(5) Super(2.5) Ultra(1) Hyper(0.5) Extreme(0.25) Unimaginable(0.1)"
    read -r -p "  Level [Ultra]: " zoom_in
    case "${zoom_in}" in
        Normal|N)           ZOOM_LEVEL="Normal" ;;
        Extra|T)            ZOOM_LEVEL="Extra" ;;
        Super|S)            ZOOM_LEVEL="Super" ;;
        Ultra|U|"")         ZOOM_LEVEL="Ultra" ;;
        Hyper|H)            ZOOM_LEVEL="Hyper" ;;
        Extreme|X)          ZOOM_LEVEL="Extreme" ;;
        Unimaginable|I)     ZOOM_LEVEL="Unimaginable" ;;
    esac
fi

# ── Summary ─────────────────────────────────────────────────────
bool_str() {
    [[ "$1" == "ON" ]] && echo -e "${GREEN}ON${NC}" || echo -e "${DIM}OFF${NC}"
}

echo ""
echo -e "${YELLOW}Configuration:${NC}"
echo -ne "  Portable:     "; bool_str "$PORTABLE"
echo -e  "  Zoom Level:   ${GREEN}${ZOOM_LEVEL}${NC}"
echo ""

# ── CMake configure ─────────────────────────────────────────────
mkdir -p build
cd build

echo -e "${CYAN}Configuring...${NC}"
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DADOCAO_PORTABLE="$PORTABLE" \
    -DADOCAO_ZOOM_LEVEL="$ZOOM_LEVEL"
echo -e "${DIM}  OK${NC}"

# ── CMake build ─────────────────────────────────────────────────
echo -e "${CYAN}Building...${NC}"
cmake --build . --parallel "$(nproc 2>/dev/null || echo 4)" 2>&1 | while IFS= read -r line; do
    if [[ "$line" =~ \[[[:space:]]*([0-9]+)%\] ]]; then
        pct="${BASH_REMATCH[1]}"
        if (( pct % 10 == 0 )); then
            echo -e "${GREEN}$line${NC}"
        fi
    elif [[ "$line" =~ [Ee]rror|fatal ]]; then
        echo -e "${RED}$line${NC}"
    elif [[ "$line" =~ warning ]]; then
        echo -e "${YELLOW}$line${NC}"
    fi
done

# ── Done ─────────────────────────────────────────────────────────
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════╗"
echo -e "${GREEN}║            Build successful!             ║"
echo -e "${GREEN}╚══════════════════════════════════════════╝"
echo ""
echo -ne "  "; echo -e "${GREEN}$(realpath ADOCAO.exe 2>/dev/null || echo "$PWD/ADOCAO.exe")${NC}"
echo ""
