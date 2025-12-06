#!/bin/bash

# Clean up
ipcs -m | grep $(whoami) | awk '{print $2}' | xargs -r ipcrm -m 2>/dev/null

# Start master
./build/eshm_demo master test_retry &
MASTER_PID=$!
sleep 1

# Start slave  
./build/eshm_demo slave test_retry &
SLAVE_PID=$!
sleep 2

echo "[TEST] Killing master..."
kill -9 $MASTER_PID

sleep 6

if ps -p $SLAVE_PID > /dev/null 2>&1; then
    echo "[TEST] SUCCESS: Slave still running after 6 seconds!"
    echo "[TEST] Restarting master..."
    ./build/eshm_demo master test_retry &
    NEW_MASTER_PID=$!
    sleep 3
    kill -TERM $SLAVE_PID $NEW_MASTER_PID 2>/dev/null
    echo "[TEST] Test complete"
else
    echo "[TEST] FAIL: Slave exited"
fi
