"""
Python wrapper for Enhanced Shared Memory (ESHM) Library

This module provides a Python interface to the ESHM C library for high-performance
inter-process communication using shared memory.
"""

import ctypes
import os
from enum import IntEnum
from typing import Optional, Tuple

# Load the ESHM library
_lib_path = os.path.join(os.path.dirname(__file__), '..', 'build', 'libeshm.a')
if not os.path.exists(_lib_path):
    # Try alternative path
    _lib_path = os.path.join(os.path.dirname(__file__), '..', 'libeshm.so')

# For static library, we need to link it properly. Let's look for shared library instead.
# Users should build a shared library version if needed, or we'll use ctypes with the demo
_demo_path = os.path.join(os.path.dirname(__file__), '..', 'build', 'libeshm.a')

# Since we have a static library, we'll need to create a shared library
# For now, let's create a wrapper that uses ctypes to load the library
# We'll need to build a shared library version

class ESHMRole(IntEnum):
    """ESHM role types"""
    MASTER = 0
    SLAVE = 1
    AUTO = 2

class ESHMError(IntEnum):
    """ESHM error codes"""
    SUCCESS = 0
    INVALID_PARAM = -1
    SHM_CREATE = -2
    SHM_ATTACH = -3
    SHM_DETACH = -4
    SHM_DELETE = -5
    MUTEX_INIT = -6
    MUTEX_LOCK = -7
    MUTEX_UNLOCK = -8
    NO_DATA = -9
    TIMEOUT = -10
    MASTER_STALE = -11
    BUFFER_FULL = -12
    BUFFER_TOO_SMALL = -13
    NOT_INITIALIZED = -14
    ROLE_MISMATCH = -15

class ESHMDisconnectBehavior(IntEnum):
    """Disconnect behavior on stale master detection"""
    IMMEDIATELY = 0
    ON_TIMEOUT = 1
    NEVER = 2

class ESHMConfig(ctypes.Structure):
    """ESHM configuration structure"""
    _fields_ = [
        ("shm_name", ctypes.c_char_p),
        ("role", ctypes.c_int),
        ("disconnect_behavior", ctypes.c_int),
        ("stale_threshold_ms", ctypes.c_uint32),
        ("reconnect_wait_ms", ctypes.c_uint32),
        ("reconnect_retry_interval_ms", ctypes.c_uint32),
        ("max_reconnect_attempts", ctypes.c_uint32),
        ("auto_cleanup", ctypes.c_bool),
        ("use_threads", ctypes.c_bool),
    ]

class ESHMStats(ctypes.Structure):
    """ESHM statistics structure"""
    _fields_ = [
        ("master_heartbeat", ctypes.c_uint64),
        ("slave_heartbeat", ctypes.c_uint64),
        ("master_pid", ctypes.c_int),
        ("slave_pid", ctypes.c_int),
        ("master_alive", ctypes.c_bool),
        ("slave_alive", ctypes.c_bool),
        ("stale_threshold", ctypes.c_uint32),
        ("master_heartbeat_delta", ctypes.c_uint64),
        ("slave_heartbeat_delta", ctypes.c_uint64),
        ("m2s_write_count", ctypes.c_uint64),
        ("m2s_read_count", ctypes.c_uint64),
        ("s2m_write_count", ctypes.c_uint64),
        ("s2m_read_count", ctypes.c_uint64),
    ]

# We need to build a shared library version of ESHM
# For now, let's create a helper script to build it
_SHARED_LIB_PATH = os.path.join(os.path.dirname(__file__), '..', 'build', 'libeshm.so')

def _load_library():
    """Load the ESHM shared library"""
    if not os.path.exists(_SHARED_LIB_PATH):
        raise RuntimeError(
            f"ESHM shared library not found at {_SHARED_LIB_PATH}\n"
            "Please build the shared library first:\n"
            "  cd build\n"
            "  g++ -shared -fPIC -o libeshm.so ../eshm.cpp -pthread -lrt"
        )
    return ctypes.CDLL(_SHARED_LIB_PATH)

class ESHM:
    """
    Python wrapper for Enhanced Shared Memory (ESHM) Library

    Example:
        # Master process
        eshm = ESHM("my_shm", role=ESHMRole.MASTER)
        eshm.write(b"Hello, World!")

        # Slave process
        eshm = ESHM("my_shm", role=ESHMRole.SLAVE)
        data = eshm.read()
        print(f"Received: {data}")
    """

    _lib = None
    _lib_initialized = False

    def __init__(self,
                 shm_name: str,
                 role: ESHMRole = ESHMRole.AUTO,
                 disconnect_behavior: ESHMDisconnectBehavior = ESHMDisconnectBehavior.ON_TIMEOUT,
                 stale_threshold_ms: int = 100,
                 reconnect_wait_ms: int = 5000,
                 reconnect_retry_interval_ms: int = 100,
                 max_reconnect_attempts: int = 50,
                 auto_cleanup: bool = True,
                 use_threads: bool = True):
        """
        Initialize ESHM handle

        Args:
            shm_name: Shared memory name/key
            role: Role (MASTER, SLAVE, or AUTO)
            disconnect_behavior: Behavior on stale master detection
            stale_threshold_ms: Stale detection threshold in milliseconds
            reconnect_wait_ms: Total time to wait for reconnection (0 = unlimited)
            reconnect_retry_interval_ms: Interval between reconnection attempts
            max_reconnect_attempts: Maximum reconnection attempts (0 = unlimited)
            auto_cleanup: Auto cleanup on destruction
            use_threads: Use dedicated threads for heartbeat and monitoring
        """
        if not ESHM._lib_initialized:
            ESHM._lib = _load_library()
            ESHM._setup_library_functions()
            ESHM._lib_initialized = True

        # Create configuration
        config = ESHMConfig()
        config.shm_name = shm_name.encode('utf-8')
        config.role = role
        config.disconnect_behavior = disconnect_behavior
        config.stale_threshold_ms = stale_threshold_ms
        config.reconnect_wait_ms = reconnect_wait_ms
        config.reconnect_retry_interval_ms = reconnect_retry_interval_ms
        config.max_reconnect_attempts = max_reconnect_attempts
        config.auto_cleanup = auto_cleanup
        config.use_threads = use_threads

        # Initialize handle
        self._handle = ESHM._lib.eshm_init(ctypes.byref(config))
        if not self._handle:
            raise RuntimeError("Failed to initialize ESHM")

        self._shm_name = shm_name

    @classmethod
    def _setup_library_functions(cls):
        """Setup library function signatures"""
        lib = cls._lib

        # eshm_init
        lib.eshm_init.argtypes = [ctypes.POINTER(ESHMConfig)]
        lib.eshm_init.restype = ctypes.c_void_p

        # eshm_destroy
        lib.eshm_destroy.argtypes = [ctypes.c_void_p]
        lib.eshm_destroy.restype = ctypes.c_int

        # eshm_write
        lib.eshm_write.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        lib.eshm_write.restype = ctypes.c_int

        # eshm_read (simplified API)
        lib.eshm_read.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
        lib.eshm_read.restype = ctypes.c_int

        # eshm_read_ex (extended API)
        lib.eshm_read_ex.argtypes = [
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_size_t),
            ctypes.c_uint32
        ]
        lib.eshm_read_ex.restype = ctypes.c_int

        # eshm_read_data (read and decode in one operation)
        lib.eshm_read_data.argtypes = [
            ctypes.c_void_p,                    # handle
            ctypes.POINTER(ctypes.c_uint8),     # out_types
            ctypes.POINTER(ctypes.c_char_p),    # out_keys
            ctypes.c_int,                       # max_key_len
            ctypes.POINTER(ctypes.c_void_p),    # out_values
            ctypes.c_int,                       # max_items
            ctypes.POINTER(ctypes.c_int),       # item_count
            ctypes.c_uint32                     # timeout_ms
        ]
        lib.eshm_read_data.restype = ctypes.c_int

        # eshm_free_value (free values returned by eshm_read_data)
        lib.eshm_free_value.argtypes = [ctypes.c_void_p, ctypes.c_uint8]
        lib.eshm_free_value.restype = None

        # eshm_get_stats
        lib.eshm_get_stats.argtypes = [ctypes.c_void_p, ctypes.POINTER(ESHMStats)]
        lib.eshm_get_stats.restype = ctypes.c_int

        # eshm_get_role
        lib.eshm_get_role.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int)]
        lib.eshm_get_role.restype = ctypes.c_int

        # eshm_check_remote_alive
        lib.eshm_check_remote_alive.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_bool)]
        lib.eshm_check_remote_alive.restype = ctypes.c_int

        # eshm_error_string
        lib.eshm_error_string.argtypes = [ctypes.c_int]
        lib.eshm_error_string.restype = ctypes.c_char_p

    def write(self, data: bytes) -> None:
        """
        Write data to shared memory

        Args:
            data: Data to write (bytes)

        Raises:
            RuntimeError: If write fails
        """
        ret = ESHM._lib.eshm_write(self._handle, data, len(data))
        if ret != ESHMError.SUCCESS:
            raise RuntimeError(f"Write failed: {self._error_string(ret)}")

    def read(self, buffer_size: int = 4096, timeout_ms: Optional[int] = None) -> bytes:
        """
        Read data from shared memory (simplified API with default 1000ms timeout)

        Args:
            buffer_size: Maximum buffer size to read
            timeout_ms: Optional custom timeout in milliseconds (uses extended API)

        Returns:
            Data read as bytes (can be empty for event trigger)

        Raises:
            RuntimeError: If read fails
            TimeoutError: If read times out
        """
        buffer = ctypes.create_string_buffer(buffer_size)

        if timeout_ms is None:
            # Use simplified API
            bytes_read = ESHM._lib.eshm_read(self._handle, buffer, buffer_size)
            if bytes_read >= 0:
                return buffer.raw[:bytes_read]
            elif bytes_read == ESHMError.TIMEOUT:
                raise TimeoutError("Read timed out")
            else:
                raise RuntimeError(f"Read failed: {self._error_string(bytes_read)}")
        else:
            # Use extended API
            bytes_read = ctypes.c_size_t()
            ret = ESHM._lib.eshm_read_ex(
                self._handle,
                buffer,
                buffer_size,
                ctypes.byref(bytes_read),
                timeout_ms
            )
            if ret == ESHMError.SUCCESS:
                return buffer.raw[:bytes_read.value]
            elif ret == ESHMError.TIMEOUT:
                raise TimeoutError("Read timed out")
            elif ret == ESHMError.NO_DATA:
                raise RuntimeError("No data available")
            else:
                raise RuntimeError(f"Read failed: {self._error_string(ret)}")

    def try_read(self, buffer_size: int = 4096) -> Optional[bytes]:
        """
        Try to read data without blocking (non-blocking read)

        Args:
            buffer_size: Maximum buffer size to read

        Returns:
            Data read as bytes, or None if no data available
        """
        buffer = ctypes.create_string_buffer(buffer_size)
        bytes_read = ctypes.c_size_t()

        ret = ESHM._lib.eshm_read_ex(
            self._handle,
            buffer,
            buffer_size,
            ctypes.byref(bytes_read),
            0  # 0 timeout = non-blocking
        )

        if ret == ESHMError.SUCCESS:
            return buffer.raw[:bytes_read.value]
        elif ret == ESHMError.NO_DATA or ret == ESHMError.TIMEOUT:
            return None
        else:
            raise RuntimeError(f"Read failed: {self._error_string(ret)}")

    def read_data(self, timeout_ms: int = 10, max_items: int = 32) -> dict:
        """
        Read and decode data in one operation (optimized for performance)

        This method reads raw data from ESHM and decodes it in C++ before
        returning to Python, avoiding the overhead of Python-side decoding.

        Args:
            timeout_ms: Timeout in milliseconds
            max_items: Maximum number of data items expected

        Returns:
            Dictionary mapping keys to values

        Raises:
            TimeoutError: If read times out
            RuntimeError: If read/decode fails
        """
        # Allocate output buffers
        max_key_len = 64
        out_types = (ctypes.c_uint8 * max_items)()
        out_keys = (ctypes.c_char_p * max_items)()

        # Pre-allocate key buffers
        key_buffers = []
        for i in range(max_items):
            key_buf = ctypes.create_string_buffer(max_key_len)
            key_buffers.append(key_buf)
            out_keys[i] = ctypes.cast(key_buf, ctypes.c_char_p)

        out_values = (ctypes.c_void_p * max_items)()
        item_count = ctypes.c_int()

        # Call C function
        ret = ESHM._lib.eshm_read_data(
            self._handle,
            out_types,
            out_keys,
            max_key_len,
            out_values,
            max_items,
            ctypes.byref(item_count),
            timeout_ms
        )

        if ret == ESHMError.TIMEOUT:
            raise TimeoutError("Read timed out")
        elif ret != ESHMError.SUCCESS:
            raise RuntimeError(f"Read data failed: {self._error_string(ret)}")

        # Convert to Python dict
        result = {}
        for i in range(item_count.value):
            key = out_keys[i].decode('utf-8')
            dtype = out_types[i]

            # Extract value based on type
            if dtype == 0:  # INTEGER
                val_ptr = ctypes.cast(out_values[i], ctypes.POINTER(ctypes.c_int64))
                result[key] = val_ptr[0]
            elif dtype == 1:  # BOOLEAN
                val_ptr = ctypes.cast(out_values[i], ctypes.POINTER(ctypes.c_bool))
                result[key] = bool(val_ptr[0])
            elif dtype == 2:  # REAL
                val_ptr = ctypes.cast(out_values[i], ctypes.POINTER(ctypes.c_double))
                result[key] = val_ptr[0]
            elif dtype == 3:  # STRING
                val_ptr = ctypes.cast(out_values[i], ctypes.c_char_p)
                result[key] = val_ptr.value.decode('utf-8')
            elif dtype == 4:  # BINARY
                # Binary data stored as struct { uint8_t* data; size_t len; }
                class BinaryData(ctypes.Structure):
                    _fields_ = [("data", ctypes.POINTER(ctypes.c_uint8)), ("len", ctypes.c_size_t)]
                val_ptr = ctypes.cast(out_values[i], ctypes.POINTER(BinaryData))
                result[key] = bytes(val_ptr[0].data[:val_ptr[0].len])

            # Free the allocated memory using the C library function
            ESHM._lib.eshm_free_value(out_values[i], dtype)

        return result

    def get_stats(self) -> dict:
        """
        Get ESHM statistics

        Returns:
            Dictionary containing statistics
        """
        stats = ESHMStats()
        ret = ESHM._lib.eshm_get_stats(self._handle, ctypes.byref(stats))
        if ret != ESHMError.SUCCESS:
            raise RuntimeError(f"Failed to get stats: {self._error_string(ret)}")

        return {
            'master_heartbeat': stats.master_heartbeat,
            'slave_heartbeat': stats.slave_heartbeat,
            'master_pid': stats.master_pid,
            'slave_pid': stats.slave_pid,
            'master_alive': stats.master_alive,
            'slave_alive': stats.slave_alive,
            'stale_threshold': stats.stale_threshold,
            'master_heartbeat_delta': stats.master_heartbeat_delta,
            'slave_heartbeat_delta': stats.slave_heartbeat_delta,
            'm2s_write_count': stats.m2s_write_count,
            'm2s_read_count': stats.m2s_read_count,
            's2m_write_count': stats.s2m_write_count,
            's2m_read_count': stats.s2m_read_count,
        }

    def get_role(self) -> ESHMRole:
        """
        Get current role

        Returns:
            Current role (MASTER or SLAVE)
        """
        role = ctypes.c_int()
        ret = ESHM._lib.eshm_get_role(self._handle, ctypes.byref(role))
        if ret != ESHMError.SUCCESS:
            raise RuntimeError(f"Failed to get role: {self._error_string(ret)}")
        return ESHMRole(role.value)

    def is_remote_alive(self) -> bool:
        """
        Check if remote endpoint is alive

        Returns:
            True if remote is alive, False otherwise
        """
        alive = ctypes.c_bool()
        ret = ESHM._lib.eshm_check_remote_alive(self._handle, ctypes.byref(alive))
        if ret != ESHMError.SUCCESS:
            raise RuntimeError(f"Failed to check remote alive: {self._error_string(ret)}")
        return alive.value

    def _error_string(self, error_code: int) -> str:
        """Get error string for error code"""
        result = ESHM._lib.eshm_error_string(error_code)
        return result.decode('utf-8') if result else f"Unknown error ({error_code})"

    def close(self):
        """Close ESHM handle"""
        if self._handle:
            ESHM._lib.eshm_destroy(self._handle)
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

    def __repr__(self):
        """String representation"""
        try:
            role = self.get_role()
            return f"ESHM(name='{self._shm_name}', role={role.name})"
        except:
            return f"ESHM(name='{self._shm_name}')"
