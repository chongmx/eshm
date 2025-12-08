#!/bin/bash
# Simple script to run the simple_exchange example with master and slave

SHM_NAME="${1:-test_simple_ex}"
DURATION="${2:-10}"

echo "=========================================="
echo "  Simple Exchange Example"
echo "=========================================="
echo "  Shared Memory: $SHM_NAME"
echo "  Duration: ${DURATION}s"
echo "=========================================="
echo ""

# Check if executable exists
if [ ! -f "./build/examples/simple_exchange" ]; then
    echo "Error: simple_exchange not found. Please build first:"
    echo "  cd build && cmake .. && make simple_exchange"
    exit 1
fi

# Clean up any stale shared memory
rm -f "/dev/shm/$SHM_NAME" 2>/dev/null

echo "Starting Master..."
./build/examples/simple_exchange master "$SHM_NAME" &
MASTER_PID=$!
sleep 1

echo "Starting Slave..."
timeout "$DURATION" ./build/examples/simple_exchange slave "$SHM_NAME" &
SLAVE_PID=$!

# Wait for slave to finish
wait $SLAVE_PID
SLAVE_EXIT=$?

# Stop master
echo ""
echo "Stopping Master..."
kill -TERM $MASTER_PID 2>/dev/null
wait $MASTER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "  Test Complete"
echo "=========================================="

exit $SLAVE_EXIT
