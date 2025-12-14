#!/usr/bin/env python3
"""
Python version of simple_exchange - compatible with C++ simple_exchange

Exchanges data at ~1kHz using ESHM + DataHandler with ASN.1 encoding.
Can run as master or slave, fully compatible with C++ version.
"""

import sys
import time
import signal
import math
import os

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole, ESHMDisconnectBehavior
# DataHandler no longer needed - decoding happens in C++ via eshm.read_data()

running = True

def signal_handler(sig, frame):
    global running
    running = False

class Statistics:
    def __init__(self):
        self.exchanges = 0
        self.decode_errors = 0
        self.min_temp = float('inf')
        self.max_temp = float('-inf')
        self.sum_temp = 0.0
        self.min_counter = float('inf')
        self.max_counter = float('-inf')
        self.start_time = None

    def update(self, temp, counter):
        self.exchanges += 1
        self.min_temp = min(self.min_temp, temp)
        self.max_temp = max(self.max_temp, temp)
        self.sum_temp += temp
        self.min_counter = min(self.min_counter, counter)
        self.max_counter = max(self.max_counter, counter)

    def print_stats(self):
        elapsed = time.time() - self.start_time
        avg_temp = self.sum_temp / self.exchanges if self.exchanges > 0 else 0

        print(f"\n=== Statistics (after {self.exchanges} exchanges) ===")
        print(f"  Elapsed time: {elapsed:.3f} s")
        print(f"  Exchange rate: {self.exchanges / elapsed:.1f} Hz")
        print(f"  Temperature: min={self.min_temp:.2f}, max={self.max_temp:.2f}, avg={avg_temp:.2f}")
        print(f"  Counter: min={int(self.min_counter)}, max={int(self.max_counter)}")
        print(f"  Decode errors: {self.decode_errors}")

    def reset_interval(self):
        self.min_temp = float('inf')
        self.max_temp = float('-inf')
        self.sum_temp = 0.0
        self.min_counter = float('inf')
        self.max_counter = float('-inf')


def run_master(shm_name):
    print("Starting MASTER mode\n")

    try:
        eshm = ESHM(
            shm_name=shm_name,
            role=ESHMRole.MASTER,
            stale_threshold_ms=100,  # 100ms (100 checks at 1kHz)
            auto_cleanup=True
        )
    except Exception as e:
        print(f"Failed to create ESHM: {e}")
        return

    handler = DataHandler()
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    counter = 0

    print("Master ready. Waiting for slave to connect...")

    # Wait for slave
    while running and not eshm.is_remote_alive():
        time.sleep(0.1)

    if not running:
        eshm.close()
        return

    print("Slave connected! Starting data exchange at 1kHz...\n")

    while running:
        frame_start = time.perf_counter()

        # Generate data: counter, temperature, status message
        temperature = 20.0 + 5.0 * math.sin(counter * 0.01)

        items = [
            DataHandler.create_integer("counter", counter),
            DataHandler.create_real("temperature", temperature),
            DataHandler.create_string("status", "OK"),
        ]

        # Encode
        buffer = handler.encode_data_buffer(items)

        # Send via ESHM
        try:
            eshm.write(buffer)
        except Exception as e:
            print(f"Write error: {e}")
            break

        # Print every 1000th exchange
        if counter % 1000 == 0 and counter > 0:
            print(f"[Master] Exchange #{counter:4d} - temp={temperature:.2f}, buffer_size={len(buffer)} bytes")

        counter += 1

        # Sleep to maintain 1kHz
        frame_end = time.perf_counter()
        elapsed = frame_end - frame_start
        sleep_time = 0.001 - elapsed  # 1ms = 1kHz

        if sleep_time > 0:
            time.sleep(sleep_time)

    print(f"\nMaster shutting down after {counter} exchanges")
    eshm.close()


def run_slave(shm_name):
    print("Starting SLAVE mode\n")

    try:
        eshm = ESHM(
            shm_name=shm_name,
            role=ESHMRole.SLAVE,
            stale_threshold_ms=100,  # 100ms (100 checks at 1kHz)
            auto_cleanup=True
        )
    except Exception as e:
        print(f"Failed to create ESHM: {e}")
        return

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    stats = Statistics()
    stats.start_time = time.time()

    print("Slave ready. Waiting for data from master...\n")

    # Receive loop
    first_data_received = False
    successful_reads = 0
    timeout_count = 0
    while running:
        # Use optimized read_data() that decodes in C++ before returning to Python
        try:
            values = eshm.read_data(timeout_ms=10, max_items=10)
        except TimeoutError:
            timeout_count += 1
            if timeout_count % 1000 == 0:
                print(f"[DEBUG] {timeout_count} timeouts so far, still waiting...")
            continue
        except Exception as e:
            print(f"Read error: {e}")
            import traceback
            traceback.print_exc()
            break

        # Got data
        try:
            successful_reads += 1
            if not first_data_received:
                print(f"[Slave] First data received!")
                first_data_received = True

            # Values are already decoded as a Python dict
            counter = values.get("counter", -1)
            temperature = values.get("temperature", 0.0)
            status = values.get("status", "")

            stats.update(temperature, counter)

            # Track message gaps to detect missed messages
            if hasattr(stats, 'last_counter'):
                gap = counter - stats.last_counter
                if gap > 1:
                    print(f"[WARNING] Missed {gap-1} messages! (last={stats.last_counter}, current={counter})")
            stats.last_counter = counter

            # Print every 1000th exchange
            if counter % 1000 == 0 and counter > 0:
                print(f"[Slave] Exchange #{counter:4d} - temp={temperature:.2f}, status=\"{status}\" (total_reads={successful_reads})")

            # Print statistics every 5000 exchanges
            if counter % 5000 == 0 and counter > 0:
                stats.print_stats()
                stats.reset_interval()

        except Exception as e:
            stats.decode_errors += 1
            if stats.decode_errors < 10:
                print(f"Decode error: {e}")

    print("\nSlave shutting down")
    stats.print_stats()
    eshm.close()


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <master|slave> <shm_name>")
        print("\nExample:")
        print(f"  Terminal 1 (Master): {sys.argv[0]} master test_exchange")
        print(f"  Terminal 2 (Slave):  {sys.argv[0]} slave test_exchange")
        print("\nCompatible with C++ simple_exchange:")
        print(f"  Terminal 1 (Python):  {sys.argv[0]} master test_exchange")
        print(f"  Terminal 2 (C++):     ./build/examples/simple_exchange slave test_exchange")
        return 1

    mode = sys.argv[1]
    shm_name = sys.argv[2]

    if mode == "master":
        run_master(shm_name)
    elif mode == "slave":
        run_slave(shm_name)
    else:
        print(f"Invalid mode: {mode} (expected 'master' or 'slave')")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
