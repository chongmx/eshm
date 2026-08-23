"""Zero-copy NVIDIA VRAM sharing between processes. See include/eshm_cuda.h.

    from eshm.eshm_cuda import EshmCudaBuffer

    # Producer (usually C++, but Python can hold this role too):
    buf = EshmCudaBuffer.create("frame", size=640 * 480 * 4)
    arr = buf.as_cupy(shape=(480, 640, 4), dtype="uint8")   # write into this

    # Consumer:
    buf = EshmCudaBuffer.attach("frame")
    arr = buf.as_cupy(shape=(480, 640, 4), dtype="uint8")   # zero-copy read

Every CUDA driver call (cuMemCreate/Export/Import/Map) and the fd handoff
between processes happen inside libeshm_cuda - this module is a thin ctypes
shell around it, the same design as eshm.rpc: the wire protocol has exactly
one implementation, so Python and C++ cannot drift apart on it.

Wrapping the resulting pointer as a cupy array needs no CUDA calls from
Python at all: __cuda_array_interface__ is a plain-Python-value protocol
(an int pointer, a shape tuple, a dtype string) rather than a numpy/cupy ABI,
so cuda_array_interface()/as_cupy() below work unchanged regardless of which
numpy/cupy version is installed - verified across numpy 1.26/cupy 13.6 and
numpy 2.5/cupy 14.2 sharing the exact same C++-written VRAM in the same run.
"""

import ctypes
import threading

try:
    from .eshm import library_path
except ImportError:                      # running from the source tree
    from eshm import library_path

__all__ = ["EshmCudaBuffer", "ESHM_CUDA_TIMEOUT_INFINITE"]

#: Wait forever in EshmCudaBuffer.attach(). Distinct from 0, which tries once.
ESHM_CUDA_TIMEOUT_INFINITE = 0xFFFFFFFF


class _EshmCudaConfig(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char_p),
        ("size", ctypes.c_size_t),
        ("device_ordinal", ctypes.c_int),
    ]


def _bind(lib):
    lib.eshm_cuda_create.argtypes = [ctypes.POINTER(_EshmCudaConfig)]
    lib.eshm_cuda_create.restype = ctypes.c_void_p

    lib.eshm_cuda_attach.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
    lib.eshm_cuda_attach.restype = ctypes.c_void_p

    lib.eshm_cuda_get_ptr.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t)]
    lib.eshm_cuda_get_ptr.restype = ctypes.c_int

    lib.eshm_cuda_device.argtypes = [ctypes.c_void_p]
    lib.eshm_cuda_device.restype = ctypes.c_int

    lib.eshm_cuda_generation.argtypes = [ctypes.c_void_p]
    lib.eshm_cuda_generation.restype = ctypes.c_uint64

    lib.eshm_cuda_destroy.argtypes = [ctypes.c_void_p]
    lib.eshm_cuda_destroy.restype = None

    lib.eshm_cuda_get_last_error.argtypes = []
    lib.eshm_cuda_get_last_error.restype = ctypes.c_char_p
    return lib


class EshmCudaBuffer:
    """A block of VRAM shared by name, mapped into this process."""

    _lib = None
    _lock = threading.Lock()

    def __init__(self, handle, name: str):
        self._handle = handle
        self._name = name

    @classmethod
    def _library(cls):
        with cls._lock:
            if cls._lib is None:
                cls._lib = _bind(ctypes.CDLL(str(library_path("eshm_cuda"))))
        return cls._lib

    @classmethod
    def _error(cls) -> str:
        msg = cls._library().eshm_cuda_get_last_error()
        return msg.decode("utf-8", "replace") if msg else "unknown error"

    @classmethod
    def create(cls, name: str, size: int, device_ordinal: int = 0) -> "EshmCudaBuffer":
        """Allocate `size` bytes of VRAM on `device_ordinal` and publish it
        under `name`. Exactly one process should hold this role per name.
        """
        lib = cls._library()
        config = _EshmCudaConfig(name.encode("utf-8"), size, device_ordinal)
        handle = lib.eshm_cuda_create(ctypes.byref(config))
        if not handle:
            raise RuntimeError(f"eshm_cuda_create('{name}') failed: {cls._error()}")
        return cls(handle, name)

    @classmethod
    def attach(cls, name: str, timeout_ms: int = 5000) -> "EshmCudaBuffer":
        """Map the VRAM published under `name` by create() into this process.

        timeout_ms: 0 tries once and raises immediately if there is no
        producer yet; ESHM_CUDA_TIMEOUT_INFINITE retries until it succeeds.
        """
        lib = cls._library()
        handle = lib.eshm_cuda_attach(name.encode("utf-8"), ctypes.c_uint32(timeout_ms))
        if not handle:
            raise RuntimeError(f"eshm_cuda_attach('{name}') failed: {cls._error()}")
        return cls(handle, name)

    def _ptr_and_size(self):
        lib = self._library()
        devptr = ctypes.c_void_p()
        size = ctypes.c_size_t()
        rc = lib.eshm_cuda_get_ptr(self._handle, ctypes.byref(devptr), ctypes.byref(size))
        if rc != 0:
            raise RuntimeError(f"eshm_cuda_get_ptr('{self._name}') failed: {self._error()}")
        return (devptr.value or 0), size.value

    @property
    def ptr(self) -> int:
        """Process-local CUDA device pointer, as a plain integer.

        Only valid for CUDA calls in THIS process - never send it to a peer.
        """
        return self._ptr_and_size()[0]

    @property
    def size(self) -> int:
        """Mapped region size in bytes (rounded up to the driver's allocation
        granularity, so it may be slightly larger than what create() asked
        for)."""
        return self._ptr_and_size()[1]

    @property
    def device(self) -> int:
        return int(self._library().eshm_cuda_device(self._handle))

    @property
    def generation(self) -> int:
        """Reserved for future re-allocation support; currently constant."""
        return int(self._library().eshm_cuda_generation(self._handle))

    def cuda_array_interface(self, shape=None, dtype="|u1", strides=None) -> dict:
        """The __cuda_array_interface__ dict describing this buffer.

        Defaults to a flat uint8 view of the whole mapped region; pass shape/
        dtype for anything else. `dtype` accepts a numpy typestr ("|u1",
        "<f4", ...) or anything numpy.dtype() understands (numpy is imported
        lazily, only if `dtype` isn't already a typestr).
        """
        ptr, size = self._ptr_and_size()
        if shape is None:
            shape = (size,)
        typestr = dtype if _looks_like_typestr(dtype) else _to_typestr(dtype)
        iface = {"shape": tuple(shape), "typestr": typestr, "data": (ptr, False), "version": 3}
        if strides is not None:
            iface["strides"] = tuple(strides)
        return iface

    def as_cupy(self, shape=None, dtype="uint8"):
        """Zero-copy cupy.ndarray view of this buffer.

        Requires cupy (imported lazily, so the rest of this module works
        without it installed). Keeps this EshmCudaBuffer alive for as long
        as the returned array is, since freeing the buffer unmaps the VRAM
        the array points at.
        """
        import cupy as cp

        np_dtype = cp.dtype(dtype)
        if shape is None:
            shape = (self.size // np_dtype.itemsize,)

        class _Holder:
            pass

        holder = _Holder()
        holder.__cuda_array_interface__ = self.cuda_array_interface(shape=shape, dtype=np_dtype.str)
        holder._eshm_owner = self  # keeps the mapping alive as long as arr is
        return cp.asarray(holder)

    def close(self) -> None:
        """Unmap/release. A producer also stops accepting new attachers.
        Idempotent."""
        if self._handle:
            self._library().eshm_cuda_destroy(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:                # noqa: BLE001 - interpreter teardown
            pass

    def __repr__(self):
        return f"EshmCudaBuffer(name='{self._name}', device={self.device}, size={self.size})"


def _looks_like_typestr(dtype) -> bool:
    return isinstance(dtype, str) and len(dtype) >= 3 and dtype[0] in "<>|="


def _to_typestr(dtype) -> str:
    import numpy as np
    return np.dtype(dtype).str
