#!/bin/bash
# Runs both C++ <-> Python directions of example 02 back to back and reports
# whether each side decoded what the other encoded.
#
#   ./run_interop.sh [build_dir] [count]
#
# build_dir defaults to the in-tree build (build/examples/02_structured_data).

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
BIN_DIR="${1:-$REPO_ROOT/build/examples/02_structured_data}"
COUNT="${2:-50}"

if [ ! -x "$BIN_DIR/structured_sender" ]; then
    echo "error: $BIN_DIR/structured_sender not found."
    echo "       Build first:  cmake --build $REPO_ROOT/build"
    echo "       or point this script at another build:  $0 /path/to/build"
    exit 1
fi

# The bindings find libeshm.so on their own; prefer this build tree.
export ESHM_LIB="${ESHM_LIB:-$REPO_ROOT/build/libeshm.so}"

status=0

# The sender (master) waits for a receiver to attach before it starts counting,
# so starting the master first is safe and the run is deterministic.
run_case() {
    local name="$1" master_cmd="$2" slave_cmd="$3" channel="$4"
    echo
    echo "=== $name ==="
    rm -f "/dev/shm/eshm_$channel" 2>/dev/null

    timeout 60 $master_cmd &
    local master_pid=$!
    sleep 0.5

    timeout 60 $slave_cmd
    local slave_rc=$?

    wait "$master_pid"
    local master_rc=$?

    if [ $slave_rc -ne 0 ] || [ $master_rc -ne 0 ]; then
        echo "--- $name FAILED (master exit $master_rc, slave exit $slave_rc)"
        status=1
    else
        echo "--- $name ok"
    fi
}

# Direction 1: C++ encodes, Python decodes.
run_case "C++ sender -> Python receiver" \
         "$BIN_DIR/structured_sender interop_cpp_py $COUNT" \
         "python3 $HERE/peer.py receive interop_cpp_py $COUNT" \
         interop_cpp_py

# Direction 2: Python encodes, C++ decodes.
run_case "Python sender -> C++ receiver" \
         "python3 $HERE/peer.py send interop_py_cpp $COUNT" \
         "$BIN_DIR/structured_receiver interop_py_cpp $COUNT" \
         interop_py_cpp

echo
if [ $status -eq 0 ]; then
    echo "both directions ok"
else
    echo "at least one direction failed"
fi
exit $status
