# How to Run C++ ↔ Python Interop Demo

## ✅ CONFIRMED WORKING

The interoperability between C++ and Python has been tested and **WORKS CORRECTLY**.

## Prerequisites

1. Build the C++ library and demo:
```bash
cd build
cmake ..
make
cd ..
```

2. Build the Python shared library:
```bash
cd py
./build_shared_lib.sh
cd ..
```

## Test 1: C++ Master + Python Slave

### Terminal 1 (C++ Master):
```bash
./build/eshm_demo master interop_test
```

You should see:
```
[MASTER] Starting high-performance master process...
[MASTER] Initialized with role: MASTER
[MASTER] Sent: Hello from master #0
...
```

### Terminal 2 (Python Slave):
```bash
python3 py/examples/simple_slave.py interop_test
```

You should see:
```
=== ESHM Python Slave Example ===
Initialized as SLAVE
[SLAVE] Received (21 bytes): Hello from master #X
[SLAVE] Sent: ACK from Python slave #0
...
```

### Back in Terminal 1:
The C++ master should now show:
```
[MASTER] Received: ACK from Python slave #0
[MASTER] Received: ACK from Python slave #1
...
```

## Test 2: Python Master + C++ Slave

### Terminal 1 (Python Master):
```bash
python3 py/examples/simple_master.py interop_test2
```

You should see:
```
=== ESHM Python Master Example ===
Initialized as MASTER
[MASTER] Sent: Hello from Python master #0
...
```

### Terminal 2 (C++ Slave):
```bash
./build/eshm_demo slave interop_test2
```

You should see:
```
[SLAVE] Starting high-performance slave process...
[SLAVE] Received (28 bytes): Hello from Python master #X
[SLAVE] Sent: ACK from slave #0
...
```

### Back in Terminal 1:
The Python master should show received messages (though output may be buffered).

## Important Notes

### 1. Use the SAME SHM Name

Both processes **MUST** use the same SHM name. For example:
- C++: `./build/eshm_demo master MY_SHM`
- Python: `python3 py/examples/simple_slave.py MY_SHM`

### 2. Start Order

- For best results, start the **master first**, then the slave
- The slave will automatically connect when it detects the master's SHM

### 3. Clean Up Between Tests

If you get connection issues, clean up SHM segments:
```bash
killall -9 eshm_demo python3
ipcrm -a  # Remove all SHM segments (be careful on shared systems!)
```

### 4. Check SHM Segments

To see active SHM segments:
```bash
ipcs -m
```

You should see a segment with perms `666` and size `8576` bytes (the size of ES HMData structure).

## Troubleshooting

### "Slave cannot connect"
- Make sure master is running first
- Check you're using the **exact same** SHM name
- Clean up old SHM segments with `ipcrm -a`

### "No messages received"
- Wait a few seconds - there may be a connection delay
- Check both processes are actually running (`ps aux | grep eshm`)
- Look for "Received" messages in the logs

### "Permission denied"
- The SHM segments use mode `0666` so all users can access
- If still having issues, check `ipcs -m` output

## Automated Test

Run the full automated test:
```bash
./scripts/test_interop.sh
```

This will test both combinations and report success/failure.

## Why It Works

Both C++ and Python use the **same underlying C library code**:
- C++ demo links against `libeshm.a` (static) or uses the code directly
- Python loads `libeshm.so` (shared) via ctypes

Both implementations:
1. Generate the same SHM key from the name string
2. Use the same `shmget()` System V SHM calls
3. Share the exact same `ESHMData` structure layout
4. Follow the same protocol (heartbeat, sequence locks, etc.)

This means they can communicate transparently across language boundaries!
