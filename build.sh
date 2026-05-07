#!/bin/bash
set -e
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=ON -DGLFW_BUILD_X11=OFF
cmake --build . --parallel $(nproc)
echo "Build successful: ./build/adocao"
