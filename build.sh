#!/bin/bash
set -e

EXTREME_ZOOM=OFF
HIGH_FPS=OFF

for arg in "$@"; do
    case "$arg" in
        exzoom)    EXTREME_ZOOM=ON ;;
        highfps)   HIGH_FPS=ON ;;
    esac
done

mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
    -DADOCAO_EXTREME_ZOOM="$EXTREME_ZOOM" \
    -DADOCAO_HIGH_FPS="$HIGH_FPS" \
    -DGLFW_BUILD_WAYLAND=ON \
    -DGLFW_BUILD_X11=OFF
cmake --build . --parallel $(nproc)
echo "Build successful: ./build/adocao"
