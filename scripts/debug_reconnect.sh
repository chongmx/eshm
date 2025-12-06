#!/bin/bash

# Clean up any old SHM
ipcs -m | grep $(whoami) | awk '{print $2}' | xargs -r ipcrm -m 2>/dev/null

# Start master
./build/eshm_demo master test_debug &
MASTER_PID=$!
echo "Master PID: $MASTER_PID"

sleep 1

# Start slave
./build/eshm_demo slave test_debug &
SLAVE_PID=$!
echo "Slave PID: $SLAVE_PID"

sleep 2

# Kill master
echo "Killing master..."
kill -9 $MASTER_PID

# Wait a bit, slave should be in reconnect mode
sleep 1

# Let slave run for a bit
sleep 2

# Check if slave is still alive
if ps -p $SLAVE_PID > /dev/null; then
    echo "Slave still running, good!"
    # Now restart master
    ./build/eshm_demo master test_debug &
    NEW_MASTER_PID=$!
    echo "New master PID: $NEW_MASTER_PID"
    
    # Let them communicate
    sleep 3
    
    # Clean shutdown
    kill -TERM $SLAVE_PID $NEW_MASTER_PID 2>/dev/null
    sleep 1
else
    echo "Slave crashed!"
fi

wait
