#!/usr/bin/env python3
"""
Standalone ESHM Demo

This is a complete, self-contained example showing both master and slave
in a single file. Demonstrates fork() for multi-process communication.
"""

import sys
import os
import time
import signal

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole


def master_process(shm_name):
    """Master process"""
    print(f"[MASTER PID {os.getpid()}] Starting...")

    with ESHM(shm_name, role=ESHMRole.MASTER) as eshm:
        print(f"[MASTER] Initialized as {eshm.get_role().name}")

        for i in range(5):
            # Send message
            message = f"Message #{i}"
            eshm.write(message.encode('utf-8'))
            print(f"[MASTER] Sent: {message}")

            # Wait for response
            time.sleep(0.5)
            response = eshm.try_read()
            if response:
                print(f"[MASTER] Received: {response.decode('utf-8')}")

        # Print stats
        stats = eshm.get_stats()
        print(f"\n[MASTER] Statistics:")
        print(f"  Messages sent: {stats['m2s_write_count']}")
        print(f"  Responses received: {stats['s2m_read_count']}")


def slave_process(shm_name):
    """Slave process"""
    print(f"[SLAVE PID {os.getpid()}] Starting...")

    with ESHM(shm_name, role=ESHMRole.SLAVE) as eshm:
        print(f"[SLAVE] Initialized as {eshm.get_role().name}")

        messages_received = 0

        while messages_received < 5:
            try:
                # Read message
                data = eshm.read(timeout_ms=2000)

                if data:
                    message = data.decode('utf-8')
                    print(f"[SLAVE] Received: {message}")

                    # Send response
                    response = f"ACK for {message}"
                    eshm.write(response.encode('utf-8'))
                    print(f"[SLAVE] Sent: {response}")

                    messages_received += 1

            except TimeoutError:
                print("[SLAVE] Timeout waiting for message")
                break

        # Print stats
        stats = eshm.get_stats()
        print(f"\n[SLAVE] Statistics:")
        print(f"  Messages received: {stats['m2s_read_count']}")
        print(f"  Responses sent: {stats['s2m_write_count']}")


def run_demo():
    """Run the demo using fork()"""
    print("=== ESHM Standalone Demo ===\n")

    shm_name = "standalone_demo"

    # Fork to create slave process
    pid = os.fork()

    if pid == 0:
        # Child process (slave)
        time.sleep(0.5)  # Give master time to initialize
        try:
            slave_process(shm_name)
        except Exception as e:
            print(f"[SLAVE ERROR] {e}")
        finally:
            os._exit(0)
    else:
        # Parent process (master)
        try:
            master_process(shm_name)

            # Wait for slave to finish
            os.waitpid(pid, 0)

            print("\n=== Demo Complete ===")

        except KeyboardInterrupt:
            print("\n[MASTER] Interrupted!")
            os.kill(pid, signal.SIGTERM)
            os.waitpid(pid, 0)


if __name__ == "__main__":
    run_demo()
