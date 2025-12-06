#!/bin/bash

# Clean up
ipcs -m | grep $(whoami) | awk '{print $2}' | xargs -r ipcrm -m 2>/dev/null

# Start master
./build/eshm_demo master test_gdb &
MASTER_PID=$!

sleep 1

# Start slave
./build/eshm_demo slave test_gdb &
SLAVE_PID=$!

sleep 2

# Kill master
echo "Killing master at $(date)"
kill -9 $MASTER_PID

# Wait for slave to crash
sleep 5

# Check core dump
if ls core* 2>/dev/null; then
    echo "Core dump found, analyzing..."
    gdb -batch -ex "bt" ./build/eshm_demo core* 2>/dev/null | head -30
fi

# Clean up
kill -9 $SLAVE_PID 2>/dev/null
