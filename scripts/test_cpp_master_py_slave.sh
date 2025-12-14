#!/bin/bash
# Test script: C++ Master + Python Slave interoperability

set -e

SHM_NAME="test_cpp_py_interop"

echo "======================================================================"
echo "  Testing C++ Master + Python Slave Interoperability"
echo "======================================================================"
echo ""

# Cleanup
pkill -9 -f "simple_exchange" 2>/dev/null || true
rm -f /dev/shm/${SHM_NAME} 2>/dev/null || true
sleep 1

# Start C++ master first
echo "[1] Starting C++ Master..."
./build/examples/simple_exchange master ${SHM_NAME} &
CPP_PID=$!
echo "    C++ Master PID: $CPP_PID"

# Give master time to initialize, create SHM, and start writing data
# This avoids slave's stale detection triggering before first data arrives
sleep 3

# Start Python slave
echo ""
echo "[2] Starting Python Slave..."
python3 py/examples/simple_exchange.py slave ${SHM_NAME} &
PY_PID=$!
echo "    Python Slave PID: $PY_PID"

# Let them run
echo ""
echo "[3] Letting them exchange data for 8 seconds..."
sleep 8

# Stop gracefully
echo ""
echo "[4] Stopping processes..."
kill -TERM $CPP_PID $PY_PID 2>/dev/null || true
wait 2>/dev/null || true

echo ""
echo "======================================================================"
echo "  Test Complete!"
echo "======================================================================"
