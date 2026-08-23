"""Named triggers across a channel: run a function on the other side.

    from eshm.rpc import Rpc
    from eshm import ESHMRole

    rpc = Rpc("demo", role=ESHMRole.SLAVE)

    @rpc.on_call("process")
    def process():                 # runs when C++ calls "process"
        ...

    @rpc.on_event("shutting_down")
    def shutting_down():
        ...

    with rpc:                      # start()/stop()
        rpc.emit("ready")          # tell the other side
        rpc.call("recompute")      # ask it to run something

A trigger carries a name and nothing else - no arguments, no return value.
Values travel through whatever data structure the two sides already share; the
trigger only says "go look". So handlers are **level-triggered**: write the
data, fire the trigger, and the handler reads current state. Two triggers that
coalesce into one delivery are correct rather than lost, because the handler
runs once and sees the latest state. Keep handlers idempotent; never count
them (`rpc.missed` reports coalescing if you need to know).

Python never touches shared memory here. The dispatcher is a C++ thread inside
libeshm that owns the control channel and calls up into Python through a
ctypes callback - so there is exactly one implementation of the wire format,
and it is the C++ one.

Handlers run on that C++ dispatcher thread, not the main thread. ctypes
acquires the GIL for the call, so touching Python objects is safe, but a slow
handler delays every later trigger.
"""

import atexit
import ctypes
import sys
import threading
import traceback

try:
    from .eshm import ESHM, ESHMRole, ESHMError, library_path
except ImportError:                      # running from the source tree
    from eshm import ESHM, ESHMRole, ESHMError, library_path

__all__ = ["Rpc"]

# void (*)(void* user) - the handler signature the C API expects.
_HANDLER = ctypes.CFUNCTYPE(None, ctypes.c_void_p)


def _bind(lib):
    lib.eshm_rpc_create.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.eshm_rpc_create.restype = ctypes.c_void_p

    lib.eshm_rpc_destroy.argtypes = [ctypes.c_void_p]
    lib.eshm_rpc_destroy.restype = None

    for name in ("eshm_rpc_on_call", "eshm_rpc_on_event"):
        fn = getattr(lib, name)
        fn.argtypes = [ctypes.c_void_p, ctypes.c_char_p, _HANDLER, ctypes.c_void_p]
        fn.restype = ctypes.c_int

    for name in ("eshm_rpc_start", "eshm_rpc_stop"):
        fn = getattr(lib, name)
        fn.argtypes = [ctypes.c_void_p]
        fn.restype = ctypes.c_int

    for name in ("eshm_rpc_call", "eshm_rpc_emit"):
        fn = getattr(lib, name)
        fn.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        fn.restype = ctypes.c_int

    for name in ("eshm_rpc_missed", "eshm_rpc_dispatched"):
        fn = getattr(lib, name)
        fn.argtypes = [ctypes.c_void_p]
        fn.restype = ctypes.c_uint64

    lib.eshm_rpc_get_last_error.argtypes = []
    lib.eshm_rpc_get_last_error.restype = ctypes.c_char_p
    return lib


class Rpc:
    """A named-trigger dispatcher over a control channel."""

    _lib = None
    _lock = threading.Lock()

    def __init__(self, channel: str, role: ESHMRole = ESHMRole.SLAVE):
        """Open the control channel "<channel>_ctl".

        Args:
            channel: base name; the control channel is always this plus "_ctl",
                so it can never collide with your data channel
            role: MASTER creates the control channel, SLAVE attaches to it
                (retrying for ~5 s, since a slave cannot attach first)
        """
        self._handle = None
        self._started = False

        # Keep every CFUNCTYPE object alive for as long as C might call it.
        # A garbage-collected callback is a dangling function pointer, and the
        # crash lands far from the cause.
        self._thunks = []

        with Rpc._lock:
            if Rpc._lib is None:
                Rpc._lib = _bind(ctypes.CDLL(str(library_path("eshm"))))

        self._handle = Rpc._lib.eshm_rpc_create(channel.encode("utf-8"), int(role))
        if not self._handle:
            raise RuntimeError(
                f"could not open control channel '{channel}_ctl': {self._error()}")

        self._channel = channel
        atexit.register(self.close)

    # ------------------------------------------------------------------ misc
    @staticmethod
    def _error() -> str:
        msg = Rpc._lib.eshm_rpc_get_last_error()
        return msg.decode("utf-8", "replace") if msg else "unknown error"

    def _register(self, api, name: str, fn):
        def thunk(_user, _fn=fn, _name=name):
            # A handler that raises must not escape into C++ - there is no way
            # to propagate it across the boundary, and unwinding through the
            # dispatcher thread would terminate the process.
            try:
                _fn()
            except Exception:                       # noqa: BLE001 - boundary
                print(f"[eshm.rpc] handler for '{_name}' raised:", file=sys.stderr)
                traceback.print_exc()

        cb = _HANDLER(thunk)
        self._thunks.append(cb)                     # keep it alive
        rc = api(self._handle, name.encode("utf-8"), cb, None)
        if rc != ESHMError.SUCCESS:
            raise RuntimeError(f"could not register '{name}': {self._error()}")
        return fn

    # -------------------------------------------------------- registration
    def on_call(self, name: str):
        """Register the one handler for a named call. Usable as a decorator.

        Exactly one handler per name; registering again replaces it. A call
        arriving for an unknown name is reported as an error by the peer's
        dispatcher.
        """
        def decorate(fn):
            return self._register(Rpc._lib.eshm_rpc_on_call, name, fn)
        return decorate

    def on_event(self, name: str):
        """Register a handler for a named event. Usable as a decorator.

        Any number of handlers per name, run in registration order. An event
        with no handlers is ignored rather than an error.
        """
        def decorate(fn):
            return self._register(Rpc._lib.eshm_rpc_on_event, name, fn)
        return decorate

    # -------------------------------------------------------------- control
    def start(self) -> None:
        """Start the dispatcher thread. Idempotent."""
        rc = Rpc._lib.eshm_rpc_start(self._handle)
        if rc != ESHMError.SUCCESS:
            raise RuntimeError(f"could not start dispatcher: {self._error()}")
        self._started = True

    def stop(self) -> None:
        """Stop the dispatcher thread and join it. Idempotent."""
        if self._handle and self._started:
            Rpc._lib.eshm_rpc_stop(self._handle)
            self._started = False

    def close(self) -> None:
        """Stop the dispatcher and release the control channel."""
        if self._handle:
            Rpc._lib.eshm_rpc_destroy(self._handle)   # stops the thread first
            self._handle = None
            self._started = False
        self._thunks.clear()

    # --------------------------------------------------------------- firing
    def call(self, name: str) -> None:
        """Ask the peer to run its handler for `name`. Returns immediately."""
        rc = Rpc._lib.eshm_rpc_call(self._handle, name.encode("utf-8"))
        if rc != ESHMError.SUCCESS:
            raise RuntimeError(f"call('{name}') failed: {self._error()}")

    def emit(self, name: str) -> None:
        """Tell the peer that `name` happened. Returns immediately."""
        rc = Rpc._lib.eshm_rpc_emit(self._handle, name.encode("utf-8"))
        if rc != ESHMError.SUCCESS:
            raise RuntimeError(f"emit('{name}') failed: {self._error()}")

    # ---------------------------------------------------------- diagnostics
    @property
    def missed(self) -> int:
        """Triggers coalesced away before reaching us, from sequence gaps.

        Non-zero is normal under load and harmless for idempotent handlers.
        It is a red flag only if a handler has been made stateful.
        """
        return int(Rpc._lib.eshm_rpc_missed(self._handle))

    @property
    def dispatched(self) -> int:
        """Handlers actually run so far."""
        return int(Rpc._lib.eshm_rpc_dispatched(self._handle))

    # ------------------------------------------------------------ lifecycle
    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        self.close()

    def __repr__(self):
        return f"Rpc(channel='{self._channel}_ctl', running={self._started})"
