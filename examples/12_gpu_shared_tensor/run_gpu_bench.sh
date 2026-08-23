#!/bin/bash
# Sweep GPU VRAM frame streaming: resolution x pacing, C++->C++ and C++->Python.
#
#   ./run_gpu_bench.sh [build_dir] [seconds]
#
# Needs a CUDA-capable GPU and libeshm_cuda built (ESHM_ENABLE_CUDA=AUTO finds
# it automatically). The Python leg additionally needs cupy installed for
# whichever interpreter is on PATH as `python3`.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="${1:-$ROOT/build}"
SECS="${2:-8}"

BIN="$BUILD/examples/12_gpu_shared_tensor/gpu_frame_bench"
if [ ! -x "$BIN" ]; then
    echo "error: $BIN not found."
    echo "       cmake -S $ROOT -B $BUILD -DCMAKE_BUILD_TYPE=Release && cmake --build $BUILD -j\$(nproc)"
    exit 1
fi

export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-$BUILD}"
export PYTHONPATH="${PYTHONPATH:-$ROOT/py}"

grab() { grep -oP "$2" "$1" 2>/dev/null | head -1; }

# one_case <label> <consumer_cmd...> -- <width> <height> <fps>
# Consumer starts first and retries internally; sender starts a few seconds
# later once cupy (if Python) has finished importing.
one_case() {
    local label="$1" recv_cmd="$2" w="$3" h="$4" fps="$5" settle="$6"
    local name="gb$$_${w}x${h}_${fps}"
    rm -f "/dev/shm/eshm_${name}"* 2>/dev/null

    eval "timeout 60 $recv_cmd --name $name --width $w --height $h --channels 3 --seconds $SECS" \
        > /tmp/gb_recv.log 2>&1 &
    local rpid=$!
    sleep "$settle"
    timeout 30 "$BIN" send --name "$name" --width "$w" --height "$h" --channels 3 \
        --fps "$fps" --seconds "$SECS" > /tmp/gb_send.log 2>&1
    wait "$rpid" 2>/dev/null
    rm -f "/dev/shm/eshm_${name}"* 2>/dev/null

    local mbps fps_sent delivered torn age50 age99
    mbps=$(grab      /tmp/gb_recv.log 'DELIVERED RATE  \K[0-9.]+')
    fps_sent=$(grab   /tmp/gb_send.log 'frames written  [0-9]+ in [0-9.]+ s  \(\K[0-9.]+')
    delivered=$(grab  /tmp/gb_recv.log 'frames read     \K[0-9]+(?= of)')
    torn=$(grab       /tmp/gb_recv.log 'torn            \K[0-9]+')
    age50=$(grab      /tmp/gb_recv.log 'frame age       p50 \K[0-9.]+')
    age99=$(grab      /tmp/gb_recv.log 'p99 \K[0-9.]+(?= ms\s*$)')

    printf "%-28s %10s %10s %10s %10s %10s\n" \
        "$label" "${fps_sent:--}" "${mbps:--}" "${delivered:--}" "${torn:--}" "${age50:--}"
}

header() {
    echo
    printf "%-28s %10s %10s %10s %10s %10s\n" \
        "case" "send fps" "MB/s" "delivered" "torn" "age p50"
    printf "%-28s %10s %10s %10s %10s %10s\n" "" "" "" "" "" "(ms)"
    echo "------------------------------------------------------------------------------------"
}

echo "ESHM GPU VRAM frame-streaming benchmark - ${SECS}s per case"
echo "  delivered = distinct frames a trigger actually dispatched for"
echo "  torn      = header/footer seq mismatch (producer's next write raced the read)"

echo
echo "### C++ -> C++ ceiling"
header
one_case "VGA flat out"        "$BIN recv"                     640  480  0  0.3
one_case "1080p flat out"      "$BIN recv"                     1920 1080 0  0.3
one_case "1080p @ 30 fps"      "$BIN recv"                     1920 1080 30 0.3
one_case "4K @ 30 fps"         "$BIN recv"                     3840 2160 30 0.3

echo
echo "### C++ -> Python (cupy), the number that matters for a policy/model consumer"
header
one_case "VGA flat out"        "python3 $HERE/gpu_frame_drain.py" 640  480  0  6
one_case "1080p flat out"      "python3 $HERE/gpu_frame_drain.py" 1920 1080 0  6
one_case "1080p @ 30 fps"      "python3 $HERE/gpu_frame_drain.py" 1920 1080 30 6
one_case "4K @ 30 fps"         "python3 $HERE/gpu_frame_drain.py" 3840 2160 30 6

echo
echo "Realistic camera rates (paced) are the numbers to trust for production sizing;"
echo "'flat out' exists to show where a single unsynchronized VRAM buffer starts"
echo "tearing under saturation - see this directory's README for the fix (double buffer)."
