#!/bin/bash
# Build shared library for Python wrapper

set -e

cd "$(dirname "$0")/.."

echo "Building ESHM shared library for Python..."

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Build shared library - need C++17 for std::variant in data_handler
# Include data_handler and asn1 encode/decode for decoding support
g++ -shared -fPIC -o libeshm.so \
    ../src/eshm.cpp \
    ../src/data_handler.cpp \
    ../src/asn1_encode.cpp \
    ../src/asn1_decode.cpp \
    -I../include -pthread -lrt -O3 -Wall -Wextra -std=c++17

echo "Shared library built successfully: build/libeshm.so"
echo "Python wrapper is ready to use!"
