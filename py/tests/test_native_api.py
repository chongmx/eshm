#!/usr/bin/env python3
"""Tests for the ctypes bindings that sit on the C API.

Covers the two modules that need the compiled C entry points:

    data_handler_native.NativeDataHandler   -> dh_* in libeshm_data
    eshm_data.ESHMData                      -> eshm_write_data / eshm_read_data

Run directly (python3 py/tests/test_native_api.py) or through ctest.
"""

import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from data_handler_native import NativeDataHandler          # noqa: E402
from eshm import ESHMRole, library_path                    # noqa: E402
from eshm_data import ESHMData                             # noqa: E402

checks = 0
failed = 0


def check(ok: bool, what: str) -> bool:
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
    print(f"  [{'PASS' if ok else 'FAIL'}] {what}", flush=True)
    return ok


def sample_items(handler=NativeDataHandler):
    return [
        handler.create_integer("frame_id", 42),
        handler.create_boolean("recording", True),
        handler.create_real("temperature_c", 21.5),
        handler.create_string("camera", "front"),
    ]


EXPECTED = {"frame_id": 42, "recording": True, "temperature_c": 21.5, "camera": "front"}


def test_library_discovery():
    print("\n[1/3] library discovery")
    check(library_path("eshm").exists(), f"library_path('eshm') -> {library_path('eshm')}")
    check(library_path("eshm_data").exists(),
          f"library_path('eshm_data') -> {library_path('eshm_data')}")


def test_native_data_handler():
    print("\n[2/3] NativeDataHandler (dh_* C API)")
    handler = NativeDataHandler()
    try:
        buffer = handler.encode_data_buffer(sample_items())
        if not check(len(buffer) > 0, f"encode_data_buffer -> {len(buffer)} bytes"):
            return
        values = NativeDataHandler.extract_simple_values(handler.decode_data_buffer(buffer))
        check(values == EXPECTED, f"decode_data_buffer -> {values}")
    finally:
        handler.close()


def test_eshm_data_round_trip():
    print("\n[3/3] ESHMData over shared memory (eshm_write_data / eshm_read_data)")
    name = f"eshm_pynative_{os.getpid()}"

    pid = os.fork()
    if pid == 0:                                   # slave: echo what it decodes
        code = 1
        try:
            time.sleep(0.2)
            slave = ESHMData(name, role=ESHMRole.SLAVE, auto_cleanup=False)
            for _ in range(50):
                items = slave.read_data(timeout_ms=200)
                if items:
                    slave.write_data(items)
                    code = 0
                    break
            slave.close()
        finally:
            os._exit(code)

    master = ESHMData(name, role=ESHMRole.MASTER)
    received = []
    try:
        for _ in range(30):
            master.write_data(sample_items())
            received = master.read_data(timeout_ms=200)
            if received:
                break
        check(ESHMData.extract_values(received) == EXPECTED,
              f"round trip -> {ESHMData.extract_values(received)}")
        check(master.try_read_data() == [], "try_read_data returns [] when nothing is pending")
    finally:
        master.close()
        _, status = os.waitpid(pid, 0)
        check(os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, "slave process exited cleanly")


def main() -> int:
    print("Native (C API) binding tests")
    test_library_discovery()
    test_native_data_handler()
    test_eshm_data_round_trip()
    print(f"\n{checks} checks, {failed} failed -> {'PASS' if failed == 0 else 'FAIL'}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
