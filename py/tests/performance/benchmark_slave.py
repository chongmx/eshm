#!/usr/bin/env python3
"""
ESHM Benchmark Slave - Performance Testing

Based on simple_slave.py but optimized for benchmarking:
- Prints stats only every N messages (default: 1000)
- Measures actual message reception rate
- No verbose per-message output
"""

import sys
import time
import os

# Add parent directory to path to import eshm module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole, ESHMError

def main():
    # Get parameters from command line
    shm_name = sys.argv[1] if len(sys.argv) > 1 else "eshm1"
    stats_interval = int(sys.argv[2]) if len(sys.argv) > 2 else 1000

    print("=== ESHM Benchmark Slave ===")
    print(f"PID: {os.getpid()}")
    print(f"SHM Name: {shm_name}")
    print(f"Stats interval: every {stats_interval} messages\n")

    # Initialize ESHM as slave with reconnection support
    with ESHM(shm_name,
              role=ESHMRole.SLAVE,
              max_reconnect_attempts=0,  # Unlimited retries
              reconnect_retry_interval_ms=100) as eshm:

        print(f"Initialized as {eshm.get_role().name}")
        print("Benchmark running...")
        print("Press Ctrl+C to stop\n")
        sys.stdout.flush()

        message_count = 0
        start_time = time.time()
        last_print_time = start_time

        try:
            while True:
                try:
                    # Read message from master (1000ms timeout)
                    data = eshm.read()

                    if data:
                        # Decode and strip any null terminators
                        message = data.decode('utf-8').rstrip('\0')

                        # Send acknowledgment with null terminator for C++ compatibility
                        response = f"ACK from Python slave #{message_count}"
                        eshm.write((response + '\0').encode('utf-8'))

                        message_count += 1

                        # Print stats at intervals
                        if message_count % stats_interval == 0:
                            now = time.time()
                            elapsed = now - start_time
                            interval_elapsed = now - last_print_time
                            rate = message_count / elapsed if elapsed > 0 else 0
                            interval_rate = stats_interval / interval_elapsed if interval_elapsed > 0 else 0

                            print(f"[{message_count:6d}] Total: {elapsed:6.1f}s, {rate:6.1f} msg/s | "
                                  f"Interval: {interval_rate:6.1f} msg/s")
                            sys.stdout.flush()
                            last_print_time = now

                except TimeoutError:
                    # Timeout is normal - just continue
                    pass

        except KeyboardInterrupt:
            print("\n[SLAVE] Shutting down...")

        # Print final stats
        elapsed = time.time() - start_time
        rate = message_count / elapsed if elapsed > 0 else 0

        print("\n=== Final Benchmark Results ===")
        print(f"Total messages: {message_count}")
        print(f"Total time: {elapsed:.2f}s")
        print(f"Average rate: {rate:.1f} msg/s")

        stats = eshm.get_stats()
        print(f"\n=== ESHM Statistics ===")
        print(f"Master->Slave: writes={stats['m2s_write_count']}, reads={stats['m2s_read_count']}")
        print(f"Slave->Master: writes={stats['s2m_write_count']}, reads={stats['s2m_read_count']}")

if __name__ == "__main__":
    main()
