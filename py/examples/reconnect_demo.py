#!/usr/bin/env python3
"""
ESHM Reconnection Demo

This example demonstrates the automatic reconnection feature when the master crashes
and restarts. The slave will automatically retry and reconnect.
"""

import sys
import time
import os
import signal

# Add parent directory to path to import eshm module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole

def run_master():
    """Run master that can be killed and restarted"""
    print("=== ESHM Reconnection Demo - Master ===")
    print(f"PID: {os.getpid()}")
    print("You can kill this process with Ctrl+C or 'kill -9'")
    print("The slave will wait and reconnect when you restart the master\n")

    with ESHM("reconnect_demo", role=ESHMRole.MASTER) as eshm:
        print(f"Master initialized as {eshm.get_role().name}")

        message_count = 0

        try:
            while True:
                message = f"Master message #{message_count}"
                eshm.write(message.encode('utf-8'))
                print(f"[MASTER] Sent: {message}")

                # Try to read response
                response = eshm.try_read()
                if response:
                    print(f"[MASTER] Received: {response.decode('utf-8')}")

                message_count += 1
                time.sleep(1)

        except KeyboardInterrupt:
            print("\n[MASTER] Shutting down gracefully...")

def run_slave():
    """Run slave with unlimited reconnection attempts"""
    print("=== ESHM Reconnection Demo - Slave ===")
    print(f"PID: {os.getpid()}")
    print("Configured with UNLIMITED reconnection attempts")
    print("This slave will wait indefinitely for the master to restart\n")

    # Configure unlimited reconnection
    with ESHM("reconnect_demo",
              role=ESHMRole.SLAVE,
              max_reconnect_attempts=0,  # 0 = unlimited
              reconnect_wait_ms=0,  # 0 = unlimited time
              reconnect_retry_interval_ms=100) as eshm:  # Retry every 100ms

        print(f"Slave initialized as {eshm.get_role().name}")
        print("Waiting for messages...\n")

        message_count = 0
        consecutive_timeouts = 0

        try:
            while True:
                try:
                    # Read with 1000ms timeout (default)
                    data = eshm.read()

                    if data:
                        message = data.decode('utf-8')
                        print(f"[SLAVE] Received: {message}")

                        # Send response
                        response = f"ACK #{message_count}"
                        eshm.write(response.encode('utf-8'))
                        print(f"[SLAVE] Sent: {response}")

                        message_count += 1
                        consecutive_timeouts = 0

                except TimeoutError:
                    consecutive_timeouts += 1

                    # Print status every 10 timeouts (every 10 seconds)
                    if consecutive_timeouts % 10 == 0:
                        if eshm.is_remote_alive():
                            print(f"[SLAVE] Waiting for data... ({consecutive_timeouts} timeouts)")
                        else:
                            print(f"[SLAVE] Master not alive - reconnection in progress... ({consecutive_timeouts} timeouts)")

        except KeyboardInterrupt:
            print("\n[SLAVE] Shutting down...")

        # Print final stats
        stats = eshm.get_stats()
        print("\n=== Final Statistics ===")
        print(f"Messages received: {stats['m2s_read_count']}")
        print(f"Messages sent: {stats['s2m_write_count']}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python reconnect_demo.py <master|slave>")
        print("\nReconnection Demo:")
        print("1. Start slave first:  python reconnect_demo.py slave")
        print("2. Start master:       python reconnect_demo.py master")
        print("3. Kill master with:   kill -9 <master_pid>")
        print("4. Restart master:     python reconnect_demo.py master")
        print("\nThe slave will automatically reconnect when master restarts!")
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
