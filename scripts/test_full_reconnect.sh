#!/bin/bash

# Clean up
ipcs -m | grep $(whoami) | awk '{print $2}' | xargs -r ipcrm -m 2>/dev/null

# Start master
./build/eshm_demo master test_fullrecon &
MASTER_PID=$!
sleep 1

# Start slave  
./build/eshm_demo slave test_fullrecon &
SLAVE_PID=$!
sleep 2

echo "[TEST] === Killing master ==="
kill -9 $MASTER_PID

sleep 2

echo "[TEST] === Restarting master ==="
./build/eshm_demo master test_fullrecon &
NEW_MASTER_PID=$!

sleep 5

if ps -p $SLAVE_PID > /dev/null 2>&1; then
    echo "[TEST] === Slave still running, cleaning up ==="
    kill -TERM $SLAVE_PID $NEW_MASTER_PID 2>/dev/null
    sleep 1
    echo "[TEST] Test complete"
else
    echo "[TEST] Slave exited"
    kill -TERM $NEW_MASTER_PID 2>/dev/null
fi
