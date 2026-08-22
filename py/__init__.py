"""
ESHM Python Package

Enhanced Shared Memory (ESHM) library Python bindings.

    from eshm import ESHM, ESHMRole

    with ESHM("my_channel", role=ESHMRole.MASTER) as conn:
        conn.write(b"hello\0")                 # NUL-terminate for C++ peers
        reply = conn.read(buffer_size=4096, timeout_ms=200)

The library is located automatically (build tree, then the system paths and the
dynamic linker cache); set ESHM_LIB=/path/to/libeshm.so to override, and call
eshm.library_path() to see what was picked.
"""

from .eshm import (
    ESHM,
    ESHMRole,
    ESHMError,
    ESHMDisconnectBehavior,
    ESHMConfig,
    ESHMStats,
    library_path,
)

# Pure-Python ASN.1 DER codec, wire compatible with the C++ DataHandler.
from .data_handler import DataHandler, DataItem, DataType

__version__ = "1.0.0"
__all__ = [
    "ESHM",
    "ESHMRole",
    "ESHMError",
    "ESHMDisconnectBehavior",
    "ESHMConfig",
    "ESHMStats",
    "library_path",
    "DataHandler",
    "DataItem",
    "DataType",
]
