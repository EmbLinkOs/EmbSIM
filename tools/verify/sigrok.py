# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Logic captures from sigrok's session files (.sr), as PulseView and
sigrok-cli write them: a zip holding `metadata` (an INI: the samplerate, the
probes and their names, the unit size) and the samples in `logic-1-1`,
`logic-1-2`... (each sample `unitsize` bytes, bit i probe i+1).

    capture = read_sr("am2302.sr")
    capture.samplerate, capture.names
    capture.edges("SDA")  # [(t_ps, level), ...], the first the level at 0
"""
import configparser
import re
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

_UNITS = {"hz": 1, "khz": 1e3, "mhz": 1e6, "ghz": 1e9}


def _rate(text: str) -> float:
    m = re.fullmatch(r"\s*([0-9.]+)\s*([kmg]?hz)?\s*", text.lower())
    if not m:
        raise ValueError(f"samplerate {text!r}")
    return float(m.group(1)) * _UNITS[m.group(2) or "hz"]


@dataclass
class Capture:
    path: str
    samplerate: float
    names: list
    unitsize: int
    data: bytes = field(repr=False)

    @property
    def samples(self) -> int:
        return len(self.data) // self.unitsize

    def channel(self, name) -> int:
        if isinstance(name, int):
            return name
        return self.names.index(name)

    def edges(self, name):
        """The channel's level at time 0 and each change after, as (t_ps, level)."""
        ch = self.channel(name)
        byte, bit = divmod(ch, 8)
        us = self.unitsize
        data = self.data
        ps_per_sample = 1e12 / self.samplerate
        level = (data[byte] >> bit) & 1 if data else 1
        out = [(0, level)]
        for i in range(byte, len(data), us):
            v = (data[i] >> bit) & 1
            if v != level:
                level = v
                out.append((round((i // us) * ps_per_sample), v))
        return out


def read_sr(path) -> Capture:
    with zipfile.ZipFile(path) as z:
        ini = configparser.ConfigParser()
        # old sigrok versions indent some keys, which configparser would take as continuations
        text = z.read("metadata").decode("utf-8", "replace")
        ini.read_string("\n".join(line.strip() for line in text.splitlines()))
        dev = ini["device 1"]
        unitsize = int(dev.get("unitsize", "1"))
        total = int(dev.get("total probes", "8"))
        names = [dev.get(f"probe{i + 1}", f"D{i}") for i in range(total)]
        base = dev.get("capturefile", "logic-1")
        parts = sorted((n for n in z.namelist() if n == base or n.startswith(base + "-")),
                       key=lambda n: int(n.rsplit("-", 1)[1]) if n != base else 0)
        data = b"".join(z.read(n) for n in parts)
        if "samplerate" not in dev:
            raise ValueError(f"{path}: no samplerate in the metadata")
        return Capture(str(Path(path)), _rate(dev["samplerate"]), names, unitsize, data)
