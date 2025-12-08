# ESHM C++ ↔ Python Interoperability

This directory contains examples demonstrating full interoperability between C++ and Python using ESHM (Enhanced Shared Memory) and the DataHandler library with ASN.1 DER encoding.

## Overview

The interoperability tests verify that structured data can be exchanged seamlessly between C++ and Python processes using:
- **ESHM**: Lock-free shared memory IPC with master/slave roles
- **DataHandler**: ASN.1 DER encoding/decoding for structured data
- **Compatible data types**: integers (int64), booleans, doubles, and strings

## Files

### C++ Programs
- [interop_cpp_master.cpp](interop_cpp_master.cpp) - C++ Master that sends data to Python Slave
- [interop_cpp_slave.cpp](interop_cpp_slave.cpp) - C++ Slave that receives data from Python Master

### Python Programs
- [../py/data_handler.py](../py/data_handler.py) - Python DataHandler library (ASN.1 encoding/decoding)
- [../py/examples/interop_py_master.py](../py/examples/interop_py_master.py) - Python Master that sends data to C++ Slave
- [../py/examples/interop_py_slave.py](../py/examples/interop_py_slave.py) - Python Slave that receives data from C++ Master

### Test Scripts
- [test_interop.sh](test_interop.sh) - Automated test script for both directions

## Building

```bash
cd build
cmake ..
make interop_cpp_master interop_cpp_slave

# Build the shared library for Python
g++ -shared -fPIC -o libeshm.so ../src/eshm.cpp -I../include -pthread -lrt
```

## Running Tests

### Test 1: Python Master → C++ Slave

**Terminal 1 (C++ Slave):**
```bash
./build/examples/interop_cpp_slave test_py_cpp
```

**Terminal 2 (Python Master):**
```bash
python3 py/examples/interop_py_master.py test_py_cpp 100
```

**Expected Output (C++ Slave):**
```
========================================
  C++ Slave <- Python Master Test
========================================
  Shared Memory: test_py_cpp
========================================

C++ slave ready. Waiting for Python master...
Python master detected! Starting to receive data...

[C++ Slave] #  42 - First message received
[C++ Slave] #   0 - temp=25.00, enabled=true, status="OK", source="Python Master"
[C++ Slave] #  10 - temp=33.41, enabled=false, status="OK", source="Python Master"
[C++ Slave] #  20 - temp=34.09, enabled=false, status="OK", source="Python Master"
...

========================================
  C++ Slave Complete
========================================
  Received: 100 messages
  Time: 1.06 s
  Rate: 94.3 Hz
  Decode errors: 0
========================================
```

### Test 2: C++ Master → Python Slave

**Terminal 1 (Python Slave):**
```bash
python3 py/examples/interop_py_slave.py test_cpp_py
```

**Terminal 2 (C++ Master):**
```bash
./build/examples/interop_cpp_master test_cpp_py 100
```

**Expected Output (Python Slave):**
```
========================================
  Python Slave <- C++ Master Test
========================================
  Shared Memory: test_cpp_py
========================================

Python slave ready. Waiting for C++ master...
C++ master detected! Starting to receive data...

[Python Slave] #   0 - temp=20.00, enabled=True, status="OK", source="C++ Master"
[Python Slave] #  10 - temp=24.21, enabled=True, status="OK", source="C++ Master"
[Python Slave] #  20 - temp=24.55, enabled=True, status="OK", source="C++ Master"
...

========================================
  Python Slave Complete
========================================
  Received: 100 messages
  Time: 1.06 s
  Rate: 94.3 Hz
  Decode errors: 0
========================================
```

### Automated Test

Run both tests automatically:
```bash
./examples/test_interop.sh
```

## Data Format

Both tests exchange the following data structure:

| Field | Type | Example | Description |
|-------|------|---------|-------------|
| counter | INTEGER | 42 | Message sequence number |
| temperature | REAL (double) | 23.5 | Simulated sensor value |
| enabled | BOOLEAN | true | State flag |
| status | UTF8_STRING | "OK" | Status message |
| source | UTF8_STRING | "C++ Master" | Data source identifier |

### ASN.1 Encoding

The data is encoded using ASN.1 DER (Distinguished Encoding Rules) in a three-sequence protocol:
1. **Type Sequence**: [INTEGER, REAL, BOOLEAN, STRING, STRING]
2. **Key Sequence**: ["counter", "temperature", "enabled", "status", "source"]
3. **Data Sequence**: [42, 23.5, true, "OK", "C++ Master"]

### Encoded Size

- **Python → C++**: ~106 bytes per message
- **C++ → Python**: ~119 bytes per message

The size difference is due to slightly different field order and string encoding optimizations.

## Performance

Typical performance on modern hardware:
- **Exchange rate**: ~90-95 Hz with 10ms sleep between messages
- **Latency**: ~10-11ms per exchange
- **Decode errors**: 0 (perfect compatibility)
- **Data integrity**: 100% - all values decode correctly

## Compatibility

The Python DataHandler library ([py/data_handler.py](../py/data_handler.py)) is fully compatible with the C++ DataHandler library ([src/data_handler.cpp](../src/data_handler.cpp)):

### Supported Data Types

| ASN.1 Type | C++ Type | Python Type | Compatible |
|------------|----------|-------------|------------|
| INTEGER | int64_t | int | ✅ |
| BOOLEAN | bool | bool | ✅ |
| REAL | double | float | ✅ |
| UTF8_STRING | std::string | str | ✅ |
| OCTET_STRING | std::vector<uint8_t> | bytes | ✅ |

### REAL Encoding Details

Both implementations use **ISO 6093 NR3** format for REAL values:
- Header byte: `0x03` (NR3 marker)
- 8 bytes: IEEE 754 binary64 (double) in big-endian

Example: 23.5 encodes as:
```
Tag: 0x09 (REAL)
Length: 0x09 (9 bytes)
Data: 0x03 0x40 0x37 0x80 0x00 0x00 0x00 0x00 0x00
      ^^^^
      NR3   IEEE 754 binary64 representation
```

## Troubleshooting

### Python: "ESHM shared library not found"

Build the shared library:
```bash
cd build
g++ -shared -fPIC -o libeshm.so ../src/eshm.cpp -I../include -pthread -lrt
```

### Slave fails to attach

The slave role requires a master to be running first. Always start the master before the slave, or use reconnect logic.

### Decode errors

If you see decode errors, ensure both C++ and Python are using the same version of the DataHandler library. The ASN.1 encoding must match exactly.

### Clean up stale shared memory

```bash
# List all shared memory segments
ls -la /dev/shm/test_*

# Remove specific segment
rm /dev/shm/test_cpp_py
```

## See Also

- [ESHM Documentation](../README.md)
- [DataHandler API](../include/data_handler.h)
- [Python ESHM Wrapper](../py/eshm.py)
- [Simple Exchange Example](README_simple_exchange.md)
