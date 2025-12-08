#!/usr/bin/env python3
"""
Python Slave for C++ to Python interoperability test

Receives data from C++ master using ESHM and DataHandler
"""

import sys
import time
import signal
import os

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole, ESHMError
from data_handler import DataHandler

running = True

def signal_handler(sig, frame):
    global running
    running = False

def main():
    if len(sys.argv) < 2:
        print("Usage: {} <shm_name>".format(sys.argv[0]))
        print("\nExample:")
        print("  Terminal 1 (C++ Master): ./build/examples/interop_cpp_master test_interop 100")
        print("  Terminal 2 (Python Slave): {} test_interop".format(sys.argv[0]))
        return 1

    shm_name = sys.argv[1]

    print("=" * 40)
    print("  Python Slave <- C++ Master Test")
    print("=" * 40)
    print(f"  Shared Memory: {shm_name}")
    print("=" * 40)
    print()

    # Setup signal handler
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Initialize ESHM
    try:
        eshm = ESHM(
            shm_name=shm_name,
            role=ESHMRole.SLAVE,
            stale_threshold_ms=100,
            auto_cleanup=True
        )
    except Exception as e:
        print(f"Failed to create ESHM: {e}")
        return 1

    handler = DataHandler()

    print("Python slave ready. Waiting for C++ master...\n")

    # Wait for first data
    first_data = False
    while running and not first_data:
        try:
            data = eshm.try_read(buffer_size=4096)
            if data:
                first_data = True
                print("C++ master detected! Starting to receive data...\n")
                # Process this first message
                items = handler.decode_data_buffer(data)
                values = DataHandler.extract_simple_values(items)
                counter = values.get("counter", 0)
                print(f"[Python Slave] #{counter:4d} - First message received")
            else:
                time.sleep(0.01)
        except Exception as e:
            print(f"Error waiting for data: {e}")
            break

    # Statistics
    total_received = 1 if first_data else 0
    decode_errors = 0
    start_time = time.time()

    # Receive loop
    while running:
        try:
            data = eshm.try_read(buffer_size=4096)

            if data is None:
                time.sleep(0.001)
                continue

            # Decode
            items = handler.decode_data_buffer(data)
            values = DataHandler.extract_simple_values(items)

            counter = values.get("counter", -1)
            temperature = values.get("temperature", 0.0)
            enabled = values.get("enabled", False)
            status = values.get("status", "")
            source = values.get("source", "")

            total_received += 1

            # Print every 10th exchange
            if counter % 10 == 0:
                print(f"[Python Slave] #{counter:4d} - temp={temperature:5.2f}, "
                      f"enabled={enabled}, status=\"{status}\", source=\"{source}\"")

        except Exception as e:
            decode_errors += 1
            if decode_errors < 10:
                print(f"Decode error: {e}")

    end_time = time.time()
    elapsed = end_time - start_time

    print()
    print("=" * 40)
    print("  Python Slave Complete")
    print("=" * 40)
    print(f"  Received: {total_received} messages")
    print(f"  Time: {elapsed:.2f} s")
    if elapsed > 0:
        print(f"  Rate: {total_received / elapsed:.1f} Hz")
    print(f"  Decode errors: {decode_errors}")
    print("=" * 40)

    eshm.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
