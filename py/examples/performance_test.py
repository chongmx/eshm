#!/usr/bin/env python3
"""
ESHM Performance Test

This example measures the throughput and latency of ESHM in Python.
"""

import sys
import time
import os

# Add parent directory to path to import eshm module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole

# Performance tuning parameters
STATS_PRINT_INTERVAL = 10000  # Print stats every N messages

def run_master(shm_name="perf_test"):
    """Run performance test master"""
    print("=== ESHM Performance Test - Master ===")
    print(f"PID: {os.getpid()}")
    print(f"SHM Name: {shm_name}\n")

    with ESHM(shm_name, role=ESHMRole.MASTER) as eshm:
        print("Waiting for slave to connect...")

        # Wait for slave to be ready (check heartbeat)
        timeout = 10  # 10 second timeout
        start = time.time()
        while not eshm.is_remote_alive():
            if time.time() - start > timeout:
                print("ERROR: Slave did not connect within timeout!")
                return
            time.sleep(0.1)

        print("Slave connected. Starting performance test...\n")
        time.sleep(0.5)  # Give slave a moment to stabilize

        # Test 1: Throughput test
        print("=== Throughput Test ===")
        num_messages = 10000
        message = b"X" * 100  # 100 byte message

        start_time = time.time()

        for i in range(num_messages):
            eshm.write(message)

        elapsed = time.time() - start_time
        throughput = num_messages / elapsed

        print(f"Sent {num_messages} messages in {elapsed:.3f} seconds")
        print(f"Throughput: {throughput:.0f} messages/second")
        print(f"Throughput: {throughput * len(message) / 1024 / 1024:.2f} MB/s\n")

        # Test 2: Round-trip latency test
        print("=== Latency Test (Round-trip) ===")
        num_pings = 1000

        latencies = []

        for i in range(num_pings):
            # Send ping with null terminator for C++ compatibility
            ping_msg = (f"PING_{i}" + '\0').encode('utf-8')
            send_time = time.time()
            eshm.write(ping_msg)

            # Wait for pong
            while True:
                response = eshm.try_read()
                if response:
                    recv_time = time.time()
                    latency = (recv_time - send_time) * 1000000  # Convert to microseconds
                    latencies.append(latency)
                    break
                time.sleep(0.00001)  # 10us sleep

        avg_latency = sum(latencies) / len(latencies)
        min_latency = min(latencies)
        max_latency = max(latencies)

        print(f"Completed {num_pings} round-trips")
        print(f"Average latency: {avg_latency:.1f} µs")
        print(f"Min latency: {min_latency:.1f} µs")
        print(f"Max latency: {max_latency:.1f} µs\n")

        # Print final stats
        stats = eshm.get_stats()
        print("=== Final Statistics ===")
        print(f"Master->Slave writes: {stats['m2s_write_count']}")
        print(f"Slave->Master reads: {stats['s2m_read_count']}")

def run_slave(shm_name="perf_test"):
    """Run performance test slave"""
    print("=== ESHM Performance Test - Slave ===")
    print(f"PID: {os.getpid()}")
    print(f"SHM Name: {shm_name}\n")

    # Connect as slave (master must be running first)
    with ESHM(shm_name, role=ESHMRole.SLAVE) as eshm:
        print("Connected to master. Ready for performance test...")
        print(f"Printing stats every {STATS_PRINT_INTERVAL} messages\n")
        sys.stdout.flush()

        messages_received = 0
        pings_received = 0
        start_time = time.time()

        try:
            while True:
                # Read message with short timeout to match master's rate
                try:
                    data = eshm.read(timeout_ms=10)  # 10ms timeout

                    if data:
                        messages_received += 1

                        # Check if it's a ping message
                        if data.startswith(b"PING_"):
                            pings_received += 1
                            # Send pong response
                            eshm.write(b"PONG")

                        # Print progress at controlled intervals
                        if messages_received % STATS_PRINT_INTERVAL == 0:
                            elapsed = time.time() - start_time
                            msg_per_sec = messages_received / elapsed if elapsed > 0 else 0
                            print(f"[SLAVE] Messages: received={messages_received}, pings={pings_received}, "
                                  f"rate={msg_per_sec:.0f} msg/sec")
                            sys.stdout.flush()

                except TimeoutError:
                    # Timeout is normal when waiting for messages
                    pass

        except KeyboardInterrupt:
            print("\n[SLAVE] Shutting down...")

        print(f"\nTotal messages received: {messages_received}")
        print(f"Total pings responded: {pings_received}")

def main():
    global STATS_PRINT_INTERVAL

    if len(sys.argv) < 2:
        print("Usage: python performance_test.py <master|slave> [shm_name] [stats_interval]")
        print("\nPerformance Test:")
        print("IMPORTANT: Start in this order:")
        print("1. Start master: python performance_test.py master [shm_name] [stats_interval]")
        print("2. Start slave:  python performance_test.py slave [shm_name] [stats_interval]  (in another terminal)")
        print("\nParameters:")
        print("  shm_name: Shared memory name (default: 'perf_test')")
        print(f"  stats_interval: Print stats every N messages (default: {STATS_PRINT_INTERVAL})")
        print("\nFor C++ interop: Use 'eshm1' or match the C++ master's name")
        print("\nExamples:")
        print("  Python master + Python slave:")
        print("    python performance_test.py master")
        print("    python performance_test.py slave")
        print("\n  C++ master + Python slave (stats every 5000 msgs):")
        print("    ./build/eshm_demo master eshm1")
        print("    python performance_test.py slave eshm1 5000")
        return

    mode = sys.argv[1]
    shm_name = sys.argv[2] if len(sys.argv) > 2 else "perf_test"

    # Optional stats interval parameter
    if len(sys.argv) > 3:
        try:
            STATS_PRINT_INTERVAL = int(sys.argv[3])
            if STATS_PRINT_INTERVAL <= 0:
                print("Error: stats_interval must be positive")
                return
        except ValueError:
            print(f"Error: Invalid stats_interval '{sys.argv[3]}' - must be an integer")
            return

    if mode == "master":
        run_master(shm_name)
    elif mode == "slave":
        run_slave(shm_name)
    else:
        print(f"Invalid mode: {mode}")
        print("Must be 'master' or 'slave'")

if __name__ == "__main__":
    main()
