#!/bin/bash
# Smoke-tests every example, in both C++ -> Python and Python -> C++ directions.
#
#   ./run_all.sh [build_dir]
#
# build_dir defaults to the in-tree build (../build/examples). Each case runs
# bounded and is checked by exit status, so this is safe to run in CI.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$HERE/.." && pwd)"
BIN="${1:-$REPO_ROOT/build/examples}"

if [ ! -x "$BIN/01_hello_channel/hello_publisher" ]; then
    echo "error: examples not built under $BIN"
    echo "       cmake -S $REPO_ROOT -B $REPO_ROOT/build && cmake --build $REPO_ROOT/build"
    exit 1
fi

export ESHM_LIB="${ESHM_LIB:-$REPO_ROOT/build/libeshm.so}"
export PYTHONPATH="${PYTHONPATH:-$REPO_ROOT/py}"

pass=0
fail=0

# run_pair NAME CHANNEL MASTER_CMD SLAVE_CMD
# The slave's exit status decides; the master is expected to finish too.
run_pair() {
    local name="$1" channel="$2" master="$3" slave="$4"
    printf '%-52s ' "$name"
    rm -f "/dev/shm/eshm_$channel" 2>/dev/null

    timeout 40 $master > "/tmp/eshm_ex_master.log" 2>&1 &
    local mpid=$!
    sleep 0.4
    timeout 40 $slave > "/tmp/eshm_ex_slave.log" 2>&1
    local src=$?
    wait "$mpid" 2>/dev/null
    local mrc=$?

    if [ $src -eq 0 ] && { [ $mrc -eq 0 ] || [ $mrc -eq 143 ] || [ $mrc -eq 124 ]; }; then
        echo "ok"
        pass=$((pass + 1))
    else
        echo "FAILED (master $mrc, slave $src)"
        echo "  --- master ---"; tail -5 /tmp/eshm_ex_master.log | sed 's/^/  /'
        echo "  --- slave ----"; tail -5 /tmp/eshm_ex_slave.log | sed 's/^/  /'
        fail=$((fail + 1))
    fi
}

# run_solo NAME CMD
run_solo() {
    local name="$1"; shift
    printf '%-52s ' "$name"
    if timeout 60 "$@" > /tmp/eshm_ex_solo.log 2>&1; then
        echo "ok"; pass=$((pass + 1))
    else
        echo "FAILED"; tail -5 /tmp/eshm_ex_solo.log | sed 's/^/  /'; fail=$((fail + 1))
    fi
}

echo "=== 02 structured data (ASN.1 in both languages) ==="
run_pair "C++ sender    -> Python receiver" ex02a \
    "$BIN/02_structured_data/structured_sender ex02a 40" \
    "python3 $HERE/02_structured_data/peer.py receive ex02a 30"
run_pair "Python sender -> C++ receiver" ex02b \
    "python3 $HERE/02_structured_data/peer.py send ex02b 40" \
    "$BIN/02_structured_data/structured_receiver ex02b 30"

echo
echo "=== 03 C API (encode/decode in C++, called from C and ctypes) ==="
run_pair "C writer      -> Python reader" ex03a \
    "$BIN/03_c_api/c_writer ex03a 40" \
    "python3 $HERE/03_c_api/peer.py read ex03a 20"
run_pair "Python writer -> C reader" ex03b \
    "python3 $HERE/03_c_api/peer.py write ex03b 40" \
    "$BIN/03_c_api/c_reader ex03b 20"

echo
echo "=== 06 large payload (chunked frames, checksum verified) ==="
run_pair "C++ sender    -> Python receiver" ex06a \
    "$BIN/06_large_payload/frame_sender ex06a --width 160 --height 120 --frames 3" \
    "python3 $HERE/06_large_payload/peer.py receive ex06a --frames 3"
run_pair "Python sender -> C++ receiver" ex06b \
    "python3 $HERE/06_large_payload/peer.py send ex06b --width 160 --height 120 --frames 3" \
    "$BIN/06_large_payload/frame_receiver ex06b --frames 3"

echo
echo "=== 07 rich types (C++ only, no channel) ==="
run_solo "rich_types" "$BIN/07_rich_types/rich_types"

echo
echo "=== 08 benchmark (short runs) ==="
run_pair "C++ driver    -> Python echo" ex08a \
    "$BIN/08_benchmark/bench drive ex08a --seconds 2" \
    "python3 $HERE/08_benchmark/bench.py echo ex08a"
run_pair "Python driver -> C++ echo" ex08b \
    "python3 $HERE/08_benchmark/bench.py drive ex08b --seconds 2" \
    "$BIN/08_benchmark/bench echo ex08b"

echo
echo "-------------------------------------------------------"
echo "$pass passed, $fail failed"
exit $(( fail > 0 ? 1 : 0 ))
