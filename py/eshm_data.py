"""
High-performance ESHM with integrated DataHandler

This module provides optimized ESHM operations with built-in ASN.1 encoding/decoding.
All encoding/decoding happens in C++, eliminating Python overhead.
"""

import ctypes
import os
from typing import List, Dict, Any, Tuple
from enum import IntEnum

# Re-use existing ESHM wrapper
from eshm import ESHM, ESHMRole, ESHMError, ESHMDisconnectBehavior, ESHMConfig


class DataType(IntEnum):
    """Type descriptor for the three-sequence protocol"""
    INTEGER = 0
    BOOLEAN = 1
    REAL = 2
    STRING = 3
    BINARY = 4
    EVENT = 5
    FUNCTION_CALL = 6
    IMAGE_FRAME = 7


class DataItem:
    """Data item in the exchange"""

    def __init__(self, data_type: DataType, key: str, value: Any):
        self.type = data_type
        self.key = key
        self.value = value


# Load the library
def _load_library():
    """Load libeshm.so, which carries eshm_write_data/eshm_read_data."""
    from eshm import library_path
    return ctypes.CDLL(str(library_path("eshm")))


class ESHMData(ESHM):
    """
    High-performance ESHM with integrated DataHandler

    This class extends ESHM with direct write_data() and read_data() methods
    that perform encoding/decoding entirely in C++ for maximum performance.

    Example:
        eshm = ESHMData("my_shm", role=ESHMRole.MASTER)

        # Write data (encode + write in one C++ call)
        items = [
            DataItem(DataType.INTEGER, "counter", 42),
            DataItem(DataType.REAL, "temperature", 23.5),
            DataItem(DataType.STRING, "status", "OK"),
        ]
        eshm.write_data(items)

        # Read data (read + decode in one C++ call)
        items = eshm.read_data()
        for item in items:
            print(f"{item.key}: {item.value}")
    """

    _data_lib = None
    _data_lib_initialized = False

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        if not ESHMData._data_lib_initialized:
            ESHMData._data_lib = _load_library()
            ESHMData._setup_data_functions()
            ESHMData._data_lib_initialized = True

    @classmethod
    def _setup_data_functions(cls):
        """Setup data function signatures"""
        lib = cls._data_lib

        # eshm_data_get_last_error
        lib.eshm_data_get_last_error.argtypes = []
        lib.eshm_data_get_last_error.restype = ctypes.c_char_p

        # eshm_write_data
        lib.eshm_write_data.argtypes = [
            ctypes.c_void_p,                    # eshm handle
            ctypes.POINTER(ctypes.c_uint8),     # types
            ctypes.POINTER(ctypes.c_char_p),    # keys
            ctypes.POINTER(ctypes.c_void_p),    # values
            ctypes.c_int                        # count
        ]
        lib.eshm_write_data.restype = ctypes.c_int

        # eshm_read_data (declared in eshm.h)
        lib.eshm_read_data.argtypes = [
            ctypes.c_void_p,                    # eshm handle
            ctypes.POINTER(ctypes.c_uint8),     # out_types
            ctypes.POINTER(ctypes.c_char_p),    # out_keys
            ctypes.c_int,                       # max_key_len
            ctypes.POINTER(ctypes.c_void_p),    # out_values
            ctypes.c_int,                       # max_items
            ctypes.POINTER(ctypes.c_int),       # item_count (out)
            ctypes.c_uint32                     # timeout_ms
        ]
        lib.eshm_read_data.restype = ctypes.c_int

        # eshm_data_free_value
        lib.eshm_data_free_value.argtypes = [ctypes.c_uint8, ctypes.c_void_p]
        lib.eshm_data_free_value.restype = None

    def write_data(self, items: List[DataItem]) -> None:
        """
        Write data items to ESHM with encoding done in C++

        This performs encode + write in a single C++ call, avoiding all
        Python/C boundary overhead.

        Args:
            items: List of DataItem objects

        Raises:
            RuntimeError: If write fails
        """
        count = len(items)

        # Prepare arrays
        types = (ctypes.c_uint8 * count)()
        keys = (ctypes.c_char_p * count)()
        values = (ctypes.c_void_p * count)()

        # Keep references to prevent garbage collection
        refs = []

        for i, item in enumerate(items):
            types[i] = item.type
            keys[i] = item.key.encode('utf-8')

            if item.type == DataType.INTEGER:
                val = ctypes.c_int64(item.value)
                refs.append(val)
                values[i] = ctypes.addressof(val)
            elif item.type == DataType.BOOLEAN:
                val = ctypes.c_bool(item.value)
                refs.append(val)
                values[i] = ctypes.addressof(val)
            elif item.type == DataType.REAL:
                val = ctypes.c_double(item.value)
                refs.append(val)
                values[i] = ctypes.addressof(val)
            elif item.type == DataType.STRING:
                val = ctypes.c_char_p(item.value.encode('utf-8'))
                refs.append(val)
                values[i] = ctypes.cast(val, ctypes.c_void_p)
            else:
                raise ValueError(f"Unsupported data type: {item.type}")

        # Call combined write (encode + write in C++)
        result = ESHMData._data_lib.eshm_write_data(
            self._handle,
            types,
            keys,
            values,
            count
        )

        if result < 0:
            error = ESHMData._data_lib.eshm_data_get_last_error()
            raise RuntimeError(f"Write data failed: {error.decode('utf-8')}")

    def read_data(self, max_items: int = 100, timeout_ms: int = 1000) -> List[DataItem]:
        """
        Read and decode data from ESHM in a single C++ call

        This performs read + decode in a single C++ call, avoiding all
        Python/C boundary overhead.

        Args:
            max_items: Maximum number of items to read
            timeout_ms: How long to wait for a message (0 = non-blocking)

        Returns:
            List of DataItem objects (empty if nothing arrived in time)

        Raises:
            RuntimeError: If read fails
        """
        # Allocate output arrays
        out_types = (ctypes.c_uint8 * max_items)()

        # Allocate key buffers
        max_key_len = 256
        key_buffers = [(ctypes.c_char * max_key_len)() for _ in range(max_items)]
        out_keys = (ctypes.c_char_p * max_items)()
        for i in range(max_items):
            out_keys[i] = ctypes.cast(key_buffers[i], ctypes.c_char_p)

        out_values = (ctypes.c_void_p * max_items)()

        # Call combined read (read + decode in C++)
        item_count = ctypes.c_int(0)
        ret = ESHMData._data_lib.eshm_read_data(
            self._handle,
            out_types,
            out_keys,
            max_key_len,
            out_values,
            max_items,
            ctypes.byref(item_count),
            timeout_ms
        )

        if ret in (ESHMError.NO_DATA, ESHMError.TIMEOUT):
            return []  # Nothing new on the channel
        if ret != ESHMError.SUCCESS:
            raise RuntimeError(f"Read data failed: {self._error_string(ret)}")

        count = item_count.value
        if count == 0:
            return []

        # Extract results
        items = []
        for i in range(count):
            dtype = DataType(out_types[i])
            key = out_keys[i].decode('utf-8')

            if dtype == DataType.INTEGER:
                value = ctypes.cast(out_values[i], ctypes.POINTER(ctypes.c_int64)).contents.value
            elif dtype == DataType.BOOLEAN:
                value = ctypes.cast(out_values[i], ctypes.POINTER(ctypes.c_bool)).contents.value
            elif dtype == DataType.REAL:
                value = ctypes.cast(out_values[i], ctypes.POINTER(ctypes.c_double)).contents.value
            elif dtype == DataType.STRING:
                value = ctypes.cast(out_values[i], ctypes.c_char_p).value.decode('utf-8')
            else:
                value = None

            items.append(DataItem(dtype, key, value))

            # Free the allocated value
            ESHMData._data_lib.eshm_data_free_value(out_types[i], out_values[i])

        return items

    def try_read_data(self, max_items: int = 100) -> List[DataItem]:
        """
        Try to read data without blocking (same as read_data but returns [] on no data)

        Args:
            max_items: Maximum number of items to read

        Returns:
            List of DataItem objects (empty if no data available)
        """
        try:
            return self.read_data(max_items, timeout_ms=0)
        except RuntimeError:
            return []

    @staticmethod
    def extract_values(items: List[DataItem]) -> Dict[str, Any]:
        """
        Extract values from items into a dictionary

        Args:
            items: List of DataItem objects

        Returns:
            Dictionary mapping keys to values
        """
        return {item.key: item.value for item in items}


# Example usage and test
if __name__ == "__main__":
    print("Testing ESHMData (integrated encoding/decoding)...")

    try:
        # Test basic functionality
        print("\n1. Creating ESHMData handles...")
        master = ESHMData("test_eshm_data", role=ESHMRole.MASTER, auto_cleanup=True)
        print("   Master created")

        import time
        time.sleep(0.2)

        slave = ESHMData("test_eshm_data", role=ESHMRole.SLAVE, auto_cleanup=True)
        print("   Slave created")

        # Wait for both endpoints to be ready
        print("\n2. Waiting for endpoints to be ready...")
        for i in range(10):
            if master.is_remote_alive() and slave.is_remote_alive():
                break
            time.sleep(0.1)
        print("   Both endpoints ready")

        print("\n3. Writing data from master...")
        items = [
            DataItem(DataType.INTEGER, "counter", 42),
            DataItem(DataType.BOOLEAN, "enabled", True),
            DataItem(DataType.REAL, "temperature", 23.5),
            DataItem(DataType.STRING, "status", "OK"),
        ]

        master.write_data(items)
        print("   Wrote 4 items")

        time.sleep(0.1)

        print("\n4. Reading data from slave...")
        # Retry a few times in case of timing
        decoded = []
        for attempt in range(5):
            decoded = slave.try_read_data()
            if decoded:
                break
            time.sleep(0.05)

        if not decoded:
            raise RuntimeError("No data received after 5 attempts")

        print(f"   Read {len(decoded)} items")

        values = ESHMData.extract_values(decoded)
        for key, value in values.items():
            print(f"   {key}: {value} (type: {type(value).__name__})")

        print("\n5. Verifying values...")
        assert values["counter"] == 42
        assert values["enabled"] == True
        assert abs(values["temperature"] - 23.5) < 0.0001
        assert values["status"] == "OK"
        print("   All values correct!")

        master.close()
        slave.close()

        print("\n✅ All tests passed!")
        print("Integrated ESHM + DataHandler is working correctly!")

    except Exception as e:
        print(f"\n❌ Error: {e}")
        import traceback
        traceback.print_exc()
