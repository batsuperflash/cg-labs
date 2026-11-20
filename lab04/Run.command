#!/bin/zsh
set -e
cd "${0:A:h}"
if [[ -x ./bin/lab04 ]]; then
    exec ./bin/lab04
fi
cmake --preset debug
cmake --build build-debug
exec ./build-debug/vulkan-starter-app
