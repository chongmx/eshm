"""
Python wrapper for native C++ DataHandler library

This module provides a thin Python wrapper around the C++ DataHandler
implementation for maximum performance encoding/decoding.
"""

import ctypes
import os
from typing import List, Dict, Any, Union
from enum import IntEnum


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


# Load the native library
_LIB_PATH = os.path.join(os.path.dirname(__file__), '..', 'build', 'libeshm_data.so')

def _load_library():
    """Load the native DataHandler library"""
    if not os.path.exists(_LIB_PATH):
        raise RuntimeError(
            f"Native DataHandler library not found at {_LIB_PATH}\n"
            "Please build the shared library first:\n"
            "  cd build\n"
            "  g++ -shared -fPIC -o libeshm_data.so \\\n"
            "    ../src/data_handler.cpp \\\n"
            "    ../src/data_handler_c_api.cpp \\\n"
            "    ../src/asn1_encode.cpp \\\n"
            "    ../src/asn1_decode.cpp \\\n"
            "    -I../include -std=c++17"
        )
    return ctypes.CDLL(_LIB_PATH)


class NativeDataHandler:
    """
    Native C++ DataHandler wrapper for high-performance encoding/decoding

    This provides the same API as the pure Python DataHandler but uses
    native C++ implementation for ~10-100x faster encoding/decoding.

    Example:
        handler = NativeDataHandler()
        items = [
            DataItem(DataType.INTEGER, "counter", 42),
            DataItem(DataType.REAL, "temperature", 23.5),
            DataItem(DataType.STRING, "status", "OK"),
        ]
        buffer = handler.encode_data_buffer(items)
        decoded = handler.decode_data_buffer(buffer)
    """

    _lib = None
    _lib_initialized = False

    def __init__(self):
        if not NativeDataHandler._lib_initialized:
            NativeDataHandler._lib = _load_library()
            NativeDataHandler._setup_library_functions()
            NativeDataHandler._lib_initialized = True

        self._handle = NativeDataHandler._lib.dh_create()
        if not self._handle:
            error = NativeDataHandler._lib.dh_get_last_error()
            raise RuntimeError(f"Failed to create DataHandler: {error.decode('utf-8')}")

    @classmethod
    def _setup_library_functions(cls):
        """Setup C library function signatures"""
        lib = cls._lib

        # dh_get_last_error
        lib.dh_get_last_error.argtypes = []
        lib.dh_get_last_error.restype = ctypes.c_char_p

        # dh_create
        lib.dh_create.argtypes = []
        lib.dh_create.restype = ctypes.c_void_p

        # dh_destroy
        lib.dh_destroy.argtypes = [ctypes.c_void_p]
        lib.dh_destroy.restype = None

        # dh_encode
        lib.dh_encode.argtypes = [
            ctypes.c_void_p,                    # handle
            ctypes.POINTER(ctypes.c_uint8),     # types
            ctypes.POINTER(ctypes.c_char_p),    # keys
            ctypes.POINTER(ctypes.c_void_p),    # values
            ctypes.c_int,                       # count
            ctypes.POINTER(ctypes.c_uint8),     # out_buffer
            ctypes.c_int                        # out_buffer_size
        ]
        lib.dh_encode.restype = ctypes.c_int

        # dh_decode
        lib.dh_decode.argtypes = [
            ctypes.c_void_p,                    # handle
            ctypes.POINTER(ctypes.c_uint8),     # buffer
            ctypes.c_int,                       # buffer_size
            ctypes.POINTER(ctypes.c_uint8),     # out_types
            ctypes.POINTER(ctypes.c_char_p),    # out_keys
            ctypes.c_int,                       # max_key_len
            ctypes.POINTER(ctypes.c_void_p),    # out_values
            ctypes.c_int                        # max_items
        ]
        lib.dh_decode.restype = ctypes.c_int

        # dh_free_value
        lib.dh_free_value.argtypes = [ctypes.c_uint8, ctypes.c_void_p]
        lib.dh_free_value.restype = None

    @staticmethod
    def create_integer(key: str, value: int) -> DataItem:
        """Create INTEGER item"""
        return DataItem(DataType.INTEGER, key, value)

    @staticmethod
    def create_boolean(key: str, value: bool) -> DataItem:
        """Create BOOLEAN item"""
        return DataItem(DataType.BOOLEAN, key, value)

    @staticmethod
    def create_real(key: str, value: float) -> DataItem:
        """Create REAL item"""
        return DataItem(DataType.REAL, key, value)

    @staticmethod
    def create_string(key: str, value: str) -> DataItem:
        """Create STRING item"""
        return DataItem(DataType.STRING, key, value)

    @staticmethod
    def create_binary(key: str, value: bytes) -> DataItem:
        """Create BINARY item"""
        return DataItem(DataType.BINARY, key, value)

    def encode_data_buffer(self, items: List[DataItem]) -> bytes:
        """
        Encode data items to buffer using native C++ implementation

        Args:
            items: List of DataItem objects

        Returns:
            Encoded bytes
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
            elif item.type == DataType.BINARY:
                # TODO: Implement binary support
                raise NotImplementedError("Binary type not yet implemented")
            else:
                raise ValueError(f"Unsupported data type: {item.type}")

        # Allocate output buffer
        out_buffer = (ctypes.c_uint8 * 8192)()

        # Call native encode
        result = NativeDataHandler._lib.dh_encode(
            self._handle,
            types,
            keys,
            values,
            count,
            out_buffer,
            8192
        )

        if result < 0:
            error = NativeDataHandler._lib.dh_get_last_error()
            raise RuntimeError(f"Encode failed: {error.decode('utf-8')}")

        return bytes(out_buffer[:result])

    def decode_data_buffer(self, buffer: bytes) -> List[DataItem]:
        """
        Decode data buffer into items using native C++ implementation

        Args:
            buffer: Encoded bytes

        Returns:
            List of DataItem objects
        """
        buffer_size = len(buffer)
        buffer_array = (ctypes.c_uint8 * buffer_size).from_buffer_copy(buffer)

        # Allocate output arrays
        max_items = 100
        out_types = (ctypes.c_uint8 * max_items)()

        # Allocate key buffers
        max_key_len = 256
        key_buffers = [(ctypes.c_char * max_key_len)() for _ in range(max_items)]
        out_keys = (ctypes.c_char_p * max_items)()
        for i in range(max_items):
            out_keys[i] = ctypes.cast(key_buffers[i], ctypes.c_char_p)

        out_values = (ctypes.c_void_p * max_items)()

        # Call native decode
        count = NativeDataHandler._lib.dh_decode(
            self._handle,
            buffer_array,
            buffer_size,
            out_types,
            out_keys,
            max_key_len,
            out_values,
            max_items
        )

        if count < 0:
            error = NativeDataHandler._lib.dh_get_last_error()
            raise RuntimeError(f"Decode failed: {error.decode('utf-8')}")

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
            NativeDataHandler._lib.dh_free_value(out_types[i], out_values[i])

        return items

    @staticmethod
    def extract_simple_values(items: List[DataItem]) -> Dict[str, Any]:
        """
        Extract simple values from items into a dictionary

        Args:
            items: List of DataItem objects

        Returns:
            Dictionary mapping keys to values
        """
        return {item.key: item.value for item in items}

    def close(self):
        """Close the DataHandler"""
        if self._handle:
            NativeDataHandler._lib.dh_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        """Context manager entry"""
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit"""
        self.close()

    def __del__(self):
        """Destructor"""
        self.close()


# Convenience exports
DataHandler = NativeDataHandler


# Example usage and test
if __name__ == "__main__":
    print("Testing Native DataHandler...")

    try:
        handler = NativeDataHandler()

        # Create items
        items = [
            NativeDataHandler.create_integer("counter", 42),
            NativeDataHandler.create_boolean("enabled", True),
            NativeDataHandler.create_real("temperature", 23.5),
            NativeDataHandler.create_string("status", "OK"),
        ]

        # Encode
        buffer = handler.encode_data_buffer(items)
        print(f"Encoded {len(items)} items to {len(buffer)} bytes")

        # Decode
        decoded = handler.decode_data_buffer(buffer)
        values = NativeDataHandler.extract_simple_values(decoded)

        print(f"Decoded values:")
        for key, value in values.items():
            print(f"  {key}: {value} (type: {type(value).__name__})")

        # Verify
        assert values["counter"] == 42
        assert values["enabled"] == True
        assert abs(values["temperature"] - 23.5) < 0.0001
        assert values["status"] == "OK"

        handler.close()

        print("\nAll tests passed!")
        print("Native C++ DataHandler is working correctly!")

    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
