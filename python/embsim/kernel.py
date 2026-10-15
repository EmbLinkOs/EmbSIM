"""Owned ctypes binding for kernel ABI 1; no implicit build or downloads."""
import ctypes as c
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAX_TIME = (1 << 64) - 1
LOGIC = ("0", "1", "Z", "X")


def timestamp(value):
    if type(value) is not int or not 0 <= value <= MAX_TIME:
        raise ValueError("time must be an unsigned 64-bit integer in nanoseconds")
    return value


class Kernel:
    def __init__(self):
        suffix = ".dll" if sys.platform == "win32" else ".dylib" if sys.platform == "darwin" else ".so"
        path = ROOT / ("build/libembsim" + suffix)
        if not path.exists():
            raise RuntimeError("kernel is not built; run python scripts/build.py in the repository root")
        self.lib = c.CDLL(str(path))
        signatures = {
            "es_create": ([], c.c_void_p),
            "es_destroy": ([c.c_void_p], None),
            "es_add_net": ([c.c_void_p], c.c_int),
            "es_add_driver": ([c.c_void_p, c.c_int, c.c_int], c.c_int),
            "es_schedule": ([c.c_void_p, c.c_uint64, c.c_int, c.c_int], c.c_int),
            "es_advance": ([c.c_void_p, c.c_uint64], c.c_int),
            "es_read": ([c.c_void_p, c.c_int], c.c_int),
            "es_now": ([c.c_void_p], c.c_uint64),
            "es_error": ([c.c_int], c.c_char_p),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.lib, name)
            fn.argtypes, fn.restype = args, result
        self.ptr = self.lib.es_create()
        if not self.ptr:
            raise MemoryError("cannot allocate simulation kernel")

    def close(self):
        if self.ptr:
            self.lib.es_destroy(self.ptr)
            self.ptr = None

    def call(self, name, *args):
        if not self.ptr:
            raise RuntimeError("kernel is closed")
        result = getattr(self.lib, name)(self.ptr, *args)
        if result < 0:
            raise ValueError(self.lib.es_error(result).decode())
        return result

    def add_net(self):
        return self.call("es_add_net")

    def add_driver(self, net, strength=1):
        return self.call("es_add_driver", net, strength)

    def schedule(self, at, driver, value):
        self.call("es_schedule", timestamp(at), driver, value)

    def advance(self, until):
        self.call("es_advance", timestamp(until))

    def read(self, net):
        return LOGIC[self.call("es_read", net)]

    @property
    def now(self):
        if not self.ptr:
            raise RuntimeError("kernel is closed")
        return self.lib.es_now(self.ptr)
