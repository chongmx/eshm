#!/bin/bash
timeout 8 bash -c '
./build/eshm_demo master test_quick &
MASTER_PID=$!
sleep 1
./build/eshm_demo slave test_quick &
SLAVE_PID=$!
sleep 2
echo "[TEST] Killing master..."
kill -9 $MASTER_PID
sleep 3
if ps -p $SLAVE_PID > /dev/null 2>&1; then
    echo "[TEST] SUCCESS: Slave still running after master killed!"
    kill -9 $SLAVE_PID
    exit 0
else
    echo "[TEST] FAIL: Slave crashed"
    exit 1
fi
'
