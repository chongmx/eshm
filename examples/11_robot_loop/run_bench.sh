#!/bin/bash
# Sweep the rates a policy-in-the-loop robot system actually runs at, and print
# one summary table.
#
#   ./run_bench.sh [build_dir] [seconds]
#
# build_dir must have been configured with a channel big enough for one camera
# frame, e.g.
#   cmake -S . -B build-robot -DCMAKE_BUILD_TYPE=Release -DESHM_MAX_DATA_SIZE=2097152
#   cmake --build build-robot -j$(nproc)

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="${1:-$ROOT/build-robot}"
SECS="${2:-4}"

BIN="$BUILD/examples/11_robot_loop/robot_sim"
if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found."
    echo "       cmake -S $ROOT -B $BUILD -DCMAKE_BUILD_TYPE=Release -DESHM_MAX_DATA_SIZE=2097152"
    echo "       cmake --build $BUILD -j\$(nproc)"
    exit 1
fi

export PYTHONPATH="${PYTHONPATH:-$ROOT/py}"
export ESHM_LIB="${ESHM_LIB:-$BUILD/libeshm.so}"
POLICY="python3 $HERE/policy.py"

grab() { grep -oP "$2" "$1" 2>/dev/null | head -1; }

# one_case <label> <ctrl_hz> <policy_hz> <infer_ms> <cameras> <W> <H> <extra robot args>
one_case() {
    local label="$1" rate="$2" phz="$3" infer="$4" cams="$5" w="$6" h="$7"; shift 7
    local chan="rb$$_${rate}_${phz}_${cams}"

    rm -f /dev/shm/eshm_${chan}* 2>/dev/null
    timeout 90 "$BIN" --channel "$chan" --rate "$rate" --seconds "$SECS" \
        --cameras "$cams" --width "$w" --height "$h" "$@" > /tmp/rb_r.log 2>&1 &
    local rpid=$!
    sleep 0.5
    timeout 90 $POLICY --channel "$chan" --hz "$phz" --infer-ms "$infer" \
        --cameras "$cams" --width "$w" --height "$h" --seconds "$SECS" \
        > /tmp/rb_p.log 2>&1
    wait $rpid 2>/dev/null
    rm -f /dev/shm/eshm_${chan}* 2>/dev/null

    local achieved jit_p50 jit_p99 loop_p50 loop_p99 age_p50 fps mbs
    achieved=$(grab /tmp/rb_r.log 'achieved \K[0-9.]+')
    jit_p50=$(grab  /tmp/rb_r.log 'tick jitter   p50 \K[0-9.]+')
    jit_p99=$(grab  /tmp/rb_r.log 'tick jitter   p50 [0-9.]+ us   p99 \K[0-9.]+')
    loop_p50=$(grab /tmp/rb_r.log 'CLOSED LOOP   p50 \K[0-9.]+')
    loop_p99=$(grab /tmp/rb_r.log 'CLOSED LOOP   p50 [0-9.]+ ms   p99 \K[0-9.]+')
    age_p50=$(grab  /tmp/rb_p.log 'STATE AGE     p50 \K[0-9.]+')
    fps=$(grab      /tmp/rb_r.log 'cameras       [0-9]+ x \K[0-9.]+')
    mbs=$(grab      /tmp/rb_r.log 'fps, \K[0-9.]+(?= MB/s)')

    printf "%-26s %8s %9s %9s %10s %10s %10s %8s %8s\n" \
        "$label" "${achieved:--}" "${jit_p50:--}" "${jit_p99:--}" \
        "${loop_p50:--}" "${loop_p99:--}" "${age_p50:--}" \
        "${fps:--}" "${mbs:--}"
}

header() {
    echo
    printf "%-26s %8s %9s %9s %10s %10s %10s %8s %8s\n" \
        "case" "ctrl Hz" "jit p50" "jit p99" "loop p50" "loop p99" "age p50" "fps" "MB/s"
    printf "%-26s %8s %9s %9s %10s %10s %10s %8s %8s\n" \
        "" "" "(us)" "(us)" "(ms)" "(ms)" "(ms)" "" ""
    echo "---------------------------------------------------------------------------------------------------------"
}

echo "ESHM robot-loop benchmark - ${SECS}s per case"
echo "  loop = state written -> policy read -> inferred -> action read back (includes inference)"
echo "  age  = how stale the state was when the policy read it (transport only)"

echo
echo "### Control rate sweep, no cameras, policy 20 Hz / 25 ms inference"
header
one_case "25 Hz control"     25 20 25 0 640 480
one_case "100 Hz control"   100 20 25 0 640 480
one_case "500 Hz control"   500 20 25 0 640 480
one_case "1 kHz control"   1000 20 25 0 640 480
one_case "1 kHz control, --spin" 1000 20 25 0 640 480 --spin

echo
echo "### Camera load, 1 kHz control, policy 20 Hz / 25 ms inference"
header
one_case "1 cam  640x480"  1000 20 25 1 640 480
one_case "2 cam  640x480"  1000 20 25 2 640 480
one_case "4 cam  640x480"  1000 20 25 4 640 480
one_case "2 cam 1280x720"  1000 20 25 2 1280 720

echo
echo "### Policy rate, 1 kHz control, 2 cameras 640x480"
header
one_case "10 Hz / 50 ms infer" 1000 10 50 2 640 480
one_case "20 Hz / 25 ms infer" 1000 20 25 2 640 480
one_case "30 Hz / 15 ms infer" 1000 30 15 2 640 480
one_case "30 Hz /  0 ms infer" 1000 30  0 2 640 480

echo
echo "Subtract the inference time from 'loop p50' to get the transport cost."
