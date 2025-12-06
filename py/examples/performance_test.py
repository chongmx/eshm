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

def run_master():
    """Run performance test master"""
    print("=== ESHM Performance Test - Master ===")
    print(f"PID: {os.getpid()}\n")

    with ESHM("perf_test", role=ESHMRole.MASTER) as eshm:
        print("Waiting for slave to connect...")
        time.sleep(1)

        # Wait for slave to be ready
        while not eshm.is_remote_alive():
            time.sleep(0.1)

        print("Slave connected. Starting performance test...\n")

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
            # Send ping
            ping_msg = f"PING_{i}".encode('utf-8')
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

def run_slave():
    """Run performance test slave"""
    print("=== ESHM Performance Test - Slave ===")
    print(f"PID: {os.getpid()}\n")

    with ESHM("perf_test", role=ESHMRole.SLAVE) as eshm:
        print("Connected to master. Ready for performance test...\n")

        messages_received = 0
        pings_received = 0

        try:
            while True:
                # Try to read message (non-blocking)
                data = eshm.try_read()

                if data:
                    messages_received += 1

                    # Check if it's a ping message
                    if data.startswith(b"PING_"):
                        pings_received += 1
                        # Send pong response
                        eshm.write(b"PONG")

                    # Print progress every 1000 messages
                    if messages_received % 1000 == 0:
                        print(f"Received {messages_received} messages ({pings_received} pings)")

                else:
                    # Small sleep when no data
                    time.sleep(0.00001)  # 10us

        except KeyboardInterrupt:
            print("\n[SLAVE] Shutting down...")

        print(f"\nTotal messages received: {messages_received}")
        print(f"Total pings responded: {pings_received}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python performance_test.py <master|slave>")
        print("\nPerformance Test:")
        print("1. Start slave:  python performance_test.py slave")
        print("2. Start master: python performance_test.py master")
        print("\nThe master will run throughput and latency tests.")
        return

    mode = sys.argv[1]

    if mode == "master":
        run_master()
    elif mode == "slave":
        run_slave()
    else:
        print(f"Invalid mode: {mode}")
        print("Must be 'master' or 'slave'")

if __name__ == "__main__":
    main()
