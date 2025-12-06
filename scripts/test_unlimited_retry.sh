#!/bin/bash

# Clean up
ipcs -m | grep $(whoami) | awk '{print $2}' | xargs -r ipcrm -m 2>/dev/null

# Start master
./build/eshm_demo master test_unlimited &
MASTER_PID=$!
sleep 1

# Start slave with unlimited retries (we'll modify the config in code)
./build/eshm_demo slave test_unlimited &
SLAVE_PID=$!
sleep 2

echo "[TEST] Killing master, slave should retry indefinitely..."
kill -9 $MASTER_PID

# Wait for more than 50 attempts (50 * 100ms = 5 seconds)
sleep 7

if ps -p $SLAVE_PID > /dev/null 2>&1; then
    echo "[TEST] SUCCESS: Slave still running after 7 seconds (would have stopped at 5s with limit=50)"
    kill -TERM $SLAVE_PID 2>/dev/null
else
    echo "[TEST] Slave exited"
fi
