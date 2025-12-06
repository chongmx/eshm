#!/bin/bash
# Test C++ <-> Python interoperability

set -e

cd "$(dirname "$0")/.."

echo "======================================"
echo "ESHM C++ <-> Python Interop Test"
echo "======================================"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    killall -9 eshm_demo python3 2>/dev/null || true
    sleep 1
}

trap cleanup EXIT

# Test 1: C++ Master + Python Slave
echo "=== Test 1: C++ Master + Python Slave ==="
echo ""

# Start C++ master
echo "Starting C++ master..."
./build/eshm_demo master interop_test > /tmp/cpp_master.log 2>&1 &
CPP_MASTER_PID=$!
sleep 1

# Start Python slave
echo "Starting Python slave..."
timeout 5 python3 py/examples/simple_slave.py interop_test > /tmp/py_slave.log 2>&1 &
PY_SLAVE_PID=$!

# Wait for slave
sleep 4

# Check if communication happened
echo "Checking C++ master output..."
if grep -q "ACK from Python slave" /tmp/cpp_master.log; then
    echo "✓ C++ master received Python responses!"
    grep "Received: ACK" /tmp/cpp_master.log | head -3
else
    echo "✗ C++ master did NOT receive Python responses"
    echo "Master log:"
    cat /tmp/cpp_master.log
fi

echo ""
echo "Checking Python slave output..."
if grep -q "Received" /tmp/py_slave.log; then
    echo "✓ Python slave received C++ messages!"
    grep "Received" /tmp/py_slave.log | head -3
else
    echo "✗ Python slave did NOT receive C++ messages"
    echo "Slave log:"
    cat /tmp/py_slave.log
fi

# Cleanup
kill -9 $CPP_MASTER_PID $PY_SLAVE_PID 2>/dev/null || true
sleep 1

echo ""
echo "=== Test 2: Python Master + C++ Slave ==="
echo ""

# Start Python master
echo "Starting Python master..."
python3 py/examples/simple_master.py interop_test2 > /tmp/py_master.log 2>&1 &
PY_MASTER_PID=$!
sleep 1.5

# Start C++ slave
echo "Starting C++ slave..."
timeout 5 ./build/eshm_demo slave interop_test2 > /tmp/cpp_slave.log 2>&1 &
CPP_SLAVE_PID=$!

# Wait for slave
sleep 3

# Check if communication happened
echo "Checking Python master output..."
if grep -q "Received" /tmp/py_master.log; then
    echo "✓ Python master received C++ responses!"
    grep "Received" /tmp/py_master.log | head -3
else
    echo "✗ Python master did NOT receive C++ responses"
    echo "Master log:"
    tail -20 /tmp/py_master.log
fi

echo ""
echo "Checking C++ slave output..."
if grep -q "Received.*Hello from Python master" /tmp/cpp_slave.log; then
    echo "✓ C++ slave received Python messages!"
    grep "Received.*Hello from Python master" /tmp/cpp_slave.log | head -3
else
    echo "✗ C++ slave did NOT receive Python messages"
    echo "Slave log:"
    cat /tmp/cpp_slave.log
fi

# Cleanup
kill -9 $PY_MASTER_PID $CPP_SLAVE_PID 2>/dev/null || true

echo ""
echo "======================================"
echo "Interop Tests Complete!"
echo "======================================"
