"""
ESHM Python Package

Enhanced Shared Memory (ESHM) library Python bindings
"""

from .eshm import (
    ESHM,
    ESHMRole,
    ESHMError,
    ESHMDisconnectBehavior,
    ESHMConfig,
    ESHMStats,
)

__version__ = "1.0.0"
__all__ = [
    "ESHM",
    "ESHMRole",
    "ESHMError",
    "ESHMDisconnectBehavior",
    "ESHMConfig",
    "ESHMStats",
]
