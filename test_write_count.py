#!/usr/bin/env python3
"""Quick test to check write_count in shared memory"""

import sys
import os
import time
sys.path.insert(0, 'py')

from eshm import ESHM, ESHMRole

def main():
    shm_name = "test_wc"

    print("Attaching to SHM as slave...")
    eshm = ESHM(
        shm_name=shm_name,
        role=ESHMRole.SLAVE,
        stale_threshold_ms=1000,
        use_threads=False,  # Disable threads for debugging
        auto_cleanup=False
    )

    print("Attached! Checking write_count...")

    # Try to peek at the write_count by reading the raw SHM
    # This is a hack but helps debug
    for i in range(10):
        result = eshm.try_read(4096)
        if result:
            print(f"  [{i}] Got data: {len(result)} bytes")
        else:
            print(f"  [{i}] No data")
        time.sleep(0.5)

    eshm.close()

if __name__ == "__main__":
    main()
