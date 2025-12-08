#!/usr/bin/env python3
"""
Python Master for Python to C++ interoperability test

Sends data to C++ slave using ESHM and DataHandler
"""

import sys
import time
import signal
import math
import os

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole
from data_handler import DataHandler

running = True

def signal_handler(sig, frame):
    global running
    running = False

def main():
    if len(sys.argv) < 2:
        print("Usage: {} <shm_name> [count]".format(sys.argv[0]))
        print("\nExample:")
        print("  Terminal 1 (Python Master): {} test_interop2 100".format(sys.argv[0]))
        print("  Terminal 2 (C++ Slave): ./build/examples/interop_cpp_slave test_interop2")
        return 1

    shm_name = sys.argv[1]
    max_count = int(sys.argv[2]) if len(sys.argv) > 2 else 100

    print("=" * 40)
    print("  Python Master -> C++ Slave Test")
    print("=" * 40)
    print(f"  Shared Memory: {shm_name}")
    print(f"  Max Count: {max_count}")
    print("=" * 40)
    print()

    # Setup signal handler
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Initialize ESHM
    try:
        eshm = ESHM(
            shm_name=shm_name,
            role=ESHMRole.MASTER,
            stale_threshold_ms=100,
            auto_cleanup=True
        )
    except Exception as e:
        print(f"Failed to create ESHM: {e}")
        return 1

    handler = DataHandler()

    print("Python master ready. Waiting for C++ slave to connect...")

    # Wait for slave
    while running and not eshm.is_remote_alive():
        time.sleep(0.1)

    if not running:
        eshm.close()
        return 0

    print("C++ slave connected! Starting data exchange...\n")

    start_time = time.time()
    counter = 0

    while running and counter < max_count:
        # Generate test data
        temperature = 25.0 + 10.0 * math.sin(counter * 0.1)
        enabled = (counter % 3 == 0)

        items = [
            DataHandler.create_integer("counter", counter),
            DataHandler.create_real("temperature", temperature),
            DataHandler.create_boolean("enabled", enabled),
            DataHandler.create_string("status", "OK"),
            DataHandler.create_string("source", "Python Master"),
        ]

        # Encode
        buffer = handler.encode_data_buffer(items)

        # Send via ESHM
        try:
            eshm.write(buffer)
        except Exception as e:
            print(f"Write error: {e}")
            break

        # Print every 10th exchange
        if counter % 10 == 0:
            print(f"[Python Master] #{counter:4d} - temp={temperature:5.2f}, "
                  f"enabled={enabled}, buffer={len(buffer)} bytes")

        counter += 1
        time.sleep(0.01)

    end_time = time.time()
    elapsed = end_time - start_time

    print()
    print("=" * 40)
    print("  Python Master Complete")
    print("=" * 40)
    print(f"  Sent: {counter} messages")
    print(f"  Time: {elapsed:.2f} s")
    if elapsed > 0:
        print(f"  Rate: {counter / elapsed:.1f} Hz")
    print("=" * 40)

    eshm.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
