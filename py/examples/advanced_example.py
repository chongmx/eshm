#!/usr/bin/env python3
"""
Advanced ESHM Example

This example demonstrates advanced features:
- Custom timeouts
- Statistics monitoring
- Error handling
- Different read modes (blocking, non-blocking, custom timeout)
"""

import sys
import time
import os
import json

# Add parent directory to path to import eshm module
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from eshm import ESHM, ESHMRole

def run_advanced_master():
    """Run advanced master example"""
    print("=== Advanced ESHM Master ===")
    print(f"PID: {os.getpid()}\n")

    with ESHM("advanced_demo", role=ESHMRole.MASTER) as eshm:
        print(f"Role: {eshm.get_role().name}")

        # Send structured data (JSON)
        data = {
            "type": "sensor_data",
            "temperature": 25.5,
            "humidity": 60.2,
            "timestamp": time.time()
        }

        message = json.dumps(data)
        eshm.write(message.encode('utf-8'))
        print(f"Sent JSON data: {message}")

        # Wait for response with custom timeout (500ms)
        print("Waiting for response (500ms timeout)...")
        try:
            response = eshm.read(timeout_ms=500)
            print(f"Received response: {response.decode('utf-8')}")
        except TimeoutError:
            print("No response received (timeout)")

        # Print statistics
        print("\n=== Statistics ===")
        stats = eshm.get_stats()
        print(f"Master heartbeat: {stats['master_heartbeat']}")
        print(f"Slave heartbeat: {stats['slave_heartbeat']}")
        print(f"Master alive: {stats['master_alive']}")
        print(f"Slave alive: {stats['slave_alive']}")
        print(f"Remote alive: {eshm.is_remote_alive()}")

        # Demonstrate non-blocking read
        print("\n=== Non-blocking Read Test ===")
        for i in range(3):
            result = eshm.try_read()
            if result:
                print(f"  Read: {result.decode('utf-8')}")
            else:
                print(f"  No data available (attempt {i+1})")
            time.sleep(0.1)

def run_advanced_slave():
    """Run advanced slave example"""
    print("=== Advanced ESHM Slave ===")
    print(f"PID: {os.getpid()}\n")

    with ESHM("advanced_demo",
              role=ESHMRole.SLAVE,
              max_reconnect_attempts=10,  # Try 10 times
              reconnect_retry_interval_ms=200) as eshm:  # Every 200ms

        print(f"Role: {eshm.get_role().name}")
        print("Listening for messages...\n")

        try:
            # Read message with default timeout (1000ms)
            data = eshm.read()

            if data:
                message = data.decode('utf-8')
                print(f"Received: {message}")

                # Try to parse as JSON
                try:
                    json_data = json.loads(message)
                    print(f"Parsed JSON:")
                    for key, value in json_data.items():
                        print(f"  {key}: {value}")

                    # Send structured response
                    response = {
                        "status": "ok",
                        "received_at": time.time(),
                        "data_type": json_data.get("type", "unknown")
                    }
                    eshm.write(json.dumps(response).encode('utf-8'))
                    print(f"\nSent response: {json.dumps(response)}")

                except json.JSONDecodeError:
                    print("Message is not JSON")
                    eshm.write(b"ACK (non-JSON)")

        except TimeoutError:
            print("Timeout waiting for message")

        # Print statistics
        print("\n=== Statistics ===")
        stats = eshm.get_stats()
        print(f"Messages read from master: {stats['m2s_read_count']}")
        print(f"Messages sent to master: {stats['s2m_write_count']}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python advanced_example.py <master|slave>")
        print("\nExamples:")
        print("  Terminal 1: python advanced_example.py master")
        print("  Terminal 2: python advanced_example.py slave")
        return

    mode = sys.argv[1]

    if mode == "master":
        run_advanced_master()
    elif mode == "slave":
        run_advanced_slave()
    else:
        print(f"Invalid mode: {mode}")
        print("Must be 'master' or 'slave'")

if __name__ == "__main__":
    main()
