#!/bin/bash
# Build shared library for Python wrapper

set -e

cd "$(dirname "$0")/.."

echo "Building ESHM shared library for Python..."

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Build shared library
g++ -shared -fPIC -o libeshm.so ../eshm.cpp -pthread -lrt -O3 -Wall -Wextra -std=c++11

echo "Shared library built successfully: build/libeshm.so"
echo "Python wrapper is ready to use!"
