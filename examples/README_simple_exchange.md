# Simple Exchange Example

This example demonstrates using ESHM (Enhanced Shared Memory) with ASN.1 DataHandler to exchange structured data between master and slave processes at 1kHz.

## Overview

The `simple_exchange` program transfers ASN.1-encoded data containing:
- **Integer counter** - Incrementing value
- **Double temperature** - Simulated sensor reading (sine wave 15-25°C)
- **String status** - Status message ("OK")

The data is encoded using ASN.1 DER format and transmitted through ESHM's lock-free shared memory channel (`ESHMChannel.data[]` buffer).

## Features

- **1kHz exchange rate** - Master sends data at 1000 Hz
- **Periodic output** - Prints every 1000th exchange
- **Statistics** - Displays stats every 5000 exchanges including:
  - Exchange rate (Hz)
  - Temperature min/max/average
  - Counter range
  - Decode errors
- **ASN.1 encoding** - Uses the fixed REAL encoding (IEEE 754 binary64)
- **Lock-free communication** - Uses ESHM's sequence lock for high performance

## Building

```bash
cd build
cmake ..
make simple_exchange
```

## Usage

### Running Master and Slave

**Terminal 1 (Master):**
```bash
./build/examples/simple_exchange master test_simple_ex
```

**Terminal 2 (Slave):**
```bash
./build/examples/simple_exchange slave test_simple_ex
```

### Example Output

**Master:**
```
Starting MASTER mode
Master ready. Waiting for slave to connect...
Slave connected! Starting data exchange at 1kHz...

[Master] Exchange #1000 - temp=17.28, buffer_size=82 bytes
[Master] Exchange #2000 - temp=24.56, buffer_size=82 bytes
[Master] Exchange #3000 - temp=15.06, buffer_size=82 bytes
...
```

**Slave:**
```
Starting SLAVE mode
Slave ready. Waiting for master...
Master detected! Starting to receive data...

[Slave] Exchange #2000 - temp=24.56, status="OK"
[Slave] Exchange #3000 - temp=15.06, status="OK"
[Slave] Exchange #4000 - temp=23.73, status="OK"
[Slave] Exchange #5000 - temp=18.69, status="OK"

=== Statistics (after 5000 exchanges) ===
  Elapsed time: 6.421 s
  Exchange rate: 778.8 Hz
  Temperature: min=15.00, max=25.00, avg=19.72
  Counter: min=1, max=5000
  Decode errors: 0
```

## Performance

Typical performance on modern hardware:
- **Exchange rate**: ~780 Hz (close to target 1kHz)
- **Buffer size**: 82 bytes per message (3 fields with ASN.1 encoding)
- **Latency**: ~1.3ms per exchange
- **Decode errors**: 0 (perfect data integrity)

## How It Works

### Master Process
1. Creates ESHM as master role
2. Waits for slave to connect
3. Generates data at 1kHz:
   - Counter (increments)
   - Temperature (sine wave simulation)
   - Status string
4. Encodes data using DataHandler (ASN.1 DER)
5. Writes to ESHM channel using `eshm_write()`
6. Sleeps to maintain 1kHz timing

### Slave Process
1. Creates ESHM as slave role
2. Waits for master data
3. Reads from ESHM channel using `eshm_read()`
4. Decodes ASN.1 data using DataHandler
5. Extracts values and updates statistics
6. Prints every 1000th exchange
7. Prints statistics every 5000 exchanges

### Data Flow
```
Master                          Slave
------                          -----
Generate Data
  ↓
Encode ASN.1 (DataHandler)
  ↓
eshm_write()
  ↓
ESHMChannel.data[4096]  →  →  →  eshm_read()
  (sequence lock)                 ↓
                            Decode ASN.1 (DataHandler)
                                  ↓
                            Extract Values
                                  ↓
                            Update Statistics
```

## ASN.1 Encoding Details

The data is encoded as a sequence with 3 items:
- **counter**: INTEGER (ASN.1 tag 0x02)
- **temperature**: REAL (ASN.1 tag 0x09, IEEE 754 binary64 format)
- **status**: UTF8_STRING (ASN.1 tag 0x0C)

Total encoded size: 82 bytes including:
- Type tags (1 byte each)
- Length encodings
- Data values
- Sequence overhead

## Stopping the Programs

Press `Ctrl+C` in either terminal to cleanly shut down both master and slave.

## See Also

- [eshm.h](../include/eshm.h) - ESHM API documentation
- [data_handler.h](../include/data_handler.h) - DataHandler API
- [example_data_handler.cpp](example_data_handler.cpp) - ASN.1 encoding examples
