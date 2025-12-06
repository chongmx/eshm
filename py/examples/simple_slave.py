#!/usr/bin/env python3
"""
Simple ESHM Slave Example

This example demonstrates a basic slave process that reads messages from shared memory
and sends responses.
"""

import sys
import time
import os

# Add parent directory to path to import eshm module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole, ESHMError

def main():
    # Get SHM name from command line or use default
    shm_name = sys.argv[1] if len(sys.argv) > 1 else "eshm1"

    print("=== ESHM Python Slave Example ===")
    print(f"PID: {os.getpid()}")
    print(f"SHM Name: {shm_name}\n")

    # Initialize ESHM as slave with reconnection support
    with ESHM(shm_name,
              role=ESHMRole.SLAVE,
              max_reconnect_attempts=0,  # Unlimited retries
              reconnect_retry_interval_ms=100) as eshm:

        print(f"Initialized as {eshm.get_role().name}")
        print("Waiting for messages from master...")
        print("Press Ctrl+C to stop\n")

        message_count = 0

        try:
            while True:
                try:
                    # Read message from master (1000ms timeout)
                    data = eshm.read()

                    if data:
                        message = data.decode('utf-8')
                        print(f"[SLAVE] Received ({len(data)} bytes): {message}")

                        # Send acknowledgment
                        response = f"ACK from Python slave #{message_count}"
                        eshm.write(response.encode('utf-8'))
                        print(f"[SLAVE] Sent: {response}")

                        message_count += 1

                except TimeoutError:
                    # Timeout is normal - just continue
                    # (Could be waiting for data or reconnecting to master)
                    pass

                # Check if master is alive
                if not eshm.is_remote_alive():
                    print("[SLAVE] WARNING: Master is not alive - waiting for reconnection...")

        except KeyboardInterrupt:
            print("\n[SLAVE] Shutting down...")

        # Print final stats
        stats = eshm.get_stats()
        print("\n=== Final Statistics ===")
        print(f"Master PID: {stats['master_pid']}")
        print(f"Slave PID: {stats['slave_pid']}")
        print(f"Master->Slave: writes={stats['m2s_write_count']}, reads={stats['m2s_read_count']}")
        print(f"Slave->Master: writes={stats['s2m_write_count']}, reads={stats['s2m_read_count']}")

if __name__ == "__main__":
    main()
