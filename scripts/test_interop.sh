#!/bin/bash
# C++ <-> Python interoperability check.
#
# The actual cases live with the examples they exercise - each one is bounded
# and reports through its exit status, so this is usable in CI:
#
#   examples/run_all.sh                        every example, both directions
#   examples/02_structured_data/run_interop.sh just the ASN.1 round trip
#
# This wrapper runs the full set.

set -u
cd "$(dirname "$0")/.."

if [ ! -d build/examples ]; then
    echo "error: examples not built."
    echo "       cmake -S . -B build && cmake --build build -j\$(nproc)"
    exit 1
fi

exec ./examples/run_all.sh "$@"
