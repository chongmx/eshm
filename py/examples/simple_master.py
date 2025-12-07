#!/usr/bin/env python3
"""
Simple ESHM Master Example

This example demonstrates a basic master process that writes messages to shared memory.
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

    print("=== ESHM Python Master Example ===")
    print(f"PID: {os.getpid()}")
    print(f"SHM Name: {shm_name}\n")

    # Initialize ESHM as master
    with ESHM(shm_name, role=ESHMRole.MASTER) as eshm:
        print(f"Initialized as {eshm.get_role().name}")
        print("Press Ctrl+C to stop\n")

        message_count = 0

        try:
            while True:
                # Send message to slave with null terminator for C++ compatibility
                message = f"Hello from Python master #{message_count}"
                eshm.write((message + '\0').encode('utf-8'))
                print(f"[MASTER] Sent: {message}")

                # Try to read response from slave (non-blocking)
                response = eshm.try_read()
                if response:
                    # Decode and strip any null terminators or garbage
                    print(f"[MASTER] Received: {response.decode('utf-8').rstrip(chr(0))}")

                # Check if slave is alive
                if not eshm.is_remote_alive():
                    print("[MASTER] WARNING: Slave is not alive/connected")

                message_count += 1
                time.sleep(0.5)

        except KeyboardInterrupt:
            print("\n[MASTER] Shutting down...")

        # Print final stats
        stats = eshm.get_stats()
        print("\n=== Final Statistics ===")
        print(f"Master PID: {stats['master_pid']}")
        print(f"Slave PID: {stats['slave_pid']}")
        print(f"Master->Slave: writes={stats['m2s_write_count']}, reads={stats['m2s_read_count']}")
        print(f"Slave->Master: writes={stats['s2m_write_count']}, reads={stats['s2m_read_count']}")

if __name__ == "__main__":
    main()
