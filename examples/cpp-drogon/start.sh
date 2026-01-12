#!/bin/bash
set -e

cd "$(dirname "$0")"

# Build the project if not already built or if sources changed
if [ ! -f "build/realworld-api" ] || [ "$(find . -name '*.cc' -o -name '*.h' -newer build/realworld-api 2>/dev/null | wc -l)" -gt 0 ]; then
    mkdir -p build
    cd build
    cmake .. >/dev/null
    make -j$(nproc)
    cd ..
fi

# Run the application
exec ./build/realworld-api
