#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real parts' captures replayed into their models (01 §8: the outside of a
part matches the real one's).

Each case in tests/captures/*/cases.json names a logic capture of a real
part on its bus (a sigrok session file), the catalog part that models it and
the protocol. The host's half of the capture is played into the model by a
pattern generator at the same times; the model's answers are recorded; both
are decoded by the protocol's rules and compared.

  am230x (AM2302/DHT22): whether each read was answered, from the real part's
    first answer on (its history before the capture decides the first ones:
    a part still measuring, or idle long enough to hear a weak start); each
    timed phase's mean within tolerance; the simulated checksums.
  onewire (DS18B20): every bit the parts sent in read slots, one for one
    (the ROM search, the scratchpads, their CRCs), past the transactions that
    show what the parts held before the capture (a scratchpad read before the
    first Convert T); the presence pulses and the 0 bits' timing.

    replay_captures.py --embsim PATH [--case NAME]... [--report FILE] [-v]

Exit 1 when a case differs beyond its tolerance."""
import argparse
import json
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
from sigrok import read_sr  # noqa: E402

US = 1_000_000  # ps


def segments(edges):
    """(level, start_ps, length_ps) runs between edges."""
    return [(edges[i][1], edges[i][0], edges[i + 1][0] - edges[i][0]) for i in range(len(edges) - 1)]


# ---- the AM230x single-wire protocol (AM2302/DHT22 and its family) -------------------

AM230X_START_MIN = 90 * US  # a low this long is the host's start, not a bit


def am230x_reads(edges):
    """Each host start and the part's answer to it, if any: {start, release,
    answered, resp_end, resp_high, lows, highs, end_low, bytes}."""
    seg = segments(edges)
    reads = []
    i = 0
    while i < len(seg):
        lv, t, w = seg[i]
        if lv != 0 or w < AM230X_START_MIN:
            i += 1
            continue
        release = t + w
        r = {"start": w, "at": t, "release": release, "answered": False}
        reads.append(r)
        j = i + 1
        # the part pulls low within 200 us of the release (a host that drives
        # the line high a while after its start masks the beginning of it)
        if j + 1 < len(seg) and seg[j][0] == 1 and seg[j][2] < 200 * US and seg[j + 1][0] == 0:
            resp_low = seg[j + 1]
            frame = seg[j + 2:j + 2 + 82]
            if len(frame) >= 81 and frame[0][0] == 1:
                r["answered"] = True
                r["resp_end"] = resp_low[1] + resp_low[2] - release  # the response low's end, after the release
                r["resp_high"] = frame[0][2]
                bits = frame[1:81]
                r["lows"] = [b[2] for b in bits[0::2]]
                r["highs"] = [b[2] for b in bits[1::2]]
                r["end_low"] = frame[81][2] if len(frame) > 81 and frame[81][0] == 0 else None
                value = [1 if h > 48 * US else 0 for h in r["highs"]]
                r["bytes"] = [int("".join(map(str, value[k:k + 8])), 2) for k in range(0, 40, 8)]
                i = j + 2 + 81
                continue
        i += 1
    return reads


def am230x_waveform(reads, lead_ps, until_ps):
    """The host's starts as a pattern generator waveform: released (1) but for
    each start's low, `lead_ps` later than in the capture."""
    steps, t = [], 0
    for r in reads:
        at = r["at"] + lead_ps
        steps.append(f"1@{(at - t) / US:.3f}us")
        steps.append(f"0@{r['start'] / US:.3f}us")
        t = at + r["start"]
    steps.append(f"1@{max(1, until_ps - t) / US:.3f}us")
    return ",".join(steps)


def am230x_values(b):
    h = (b[0] << 8 | b[1]) / 10
    t = (b[2] << 8 | b[3])
    t = -(t & 0x7FFF) / 10 if t & 0x8000 else t / 10
    return h, t


def am230x_phases(reads):
    """The mean of each timed phase over the answered reads, in microseconds."""
    ans = [r for r in reads if r["answered"]]
    if not ans:
        return {}
    first = [r["lows"][k] for r in ans for k in range(0, 40, 8)]
    other = [r["lows"][k] for r in ans for k in range(40) if k % 8]
    h0 = [h for r in ans for h in r["highs"] if h <= 48 * US]
    h1 = [h for r in ans for h in r["highs"] if h > 48 * US]
    ends = [r["end_low"] for r in ans if r["end_low"] is not None]
    mean = lambda xs: statistics.mean(xs) / US if xs else None  # noqa: E731
    return {
        "response_end": mean([r["resp_end"] for r in ans]),
        "response_high": mean([r["resp_high"] for r in ans]),
        "bit_low": mean(other),
        "bit_low_first_of_byte": mean(first),
        "high_0": mean(h0),
        "high_1": mean(h1),
        "closing_low": mean(ends),
    }


def checksum_ok(b):
    return (sum(b[:4]) & 0xFF) == b[4]


# ---- the 1-Wire bus (DS18B20 and its kin) ------------------------------------------------

# A slot names itself by its low: the master's reset (480 us or more), its
# write 0 (60 us or more) and write 1 (a few us, released before the 15 us
# sample), a read slot it starts with a pulse of a few us that a part answering
# 0 stretches past 15 us; and a part's presence pulse after a reset.
OW_RESET, OW_W0, OW_R0, OW_W1 = 400 * US, 55 * US, 15 * US, 6 * US
OW_READ_START = 2 * US  # the master's start of a read slot a part stretched (hidden in the capture)


def onewire_events(edges):
    """The bus as events: ('reset', at, low), ('presence', at, low, after),
    ('w0'|'w1'|'r0'|'r1', at, low)."""
    ev = []
    after_reset = None
    for lv, t, w in segments(edges):
        if lv != 0:
            continue
        if w >= OW_RESET:
            ev.append(("reset", t, w))
            after_reset = t + w
            continue
        if after_reset is not None:  # the first low after a reset is the parts' presence
            ev.append(("presence", t, w, t - after_reset))
            after_reset = None
            continue
        kind = "w0" if w >= OW_W0 else "r0" if w >= OW_R0 else "w1" if w >= OW_W1 else "r1"
        ev.append((kind, t, w))
    return ev


def onewire_waveform(ev, lead_ps, until_ps):
    """The master's half: its resets and write slots as they were, its read slots'
    starts (the captured pulse for a 1, a typical start for a 0)."""
    lows = []
    for e in ev:
        if e[0] in ("reset", "w0", "w1", "r1"):
            lows.append((e[1], e[2]))
        elif e[0] == "r0":
            lows.append((e[1], OW_READ_START))
    steps, t = [], 0
    for at, w in lows:
        at += lead_ps
        steps.append(f"1@{(at - t) / US:.3f}us")
        steps.append(f"0@{w / US:.3f}us")
        t = at + w
    steps.append(f"1@{max(1, until_ps - t) / US:.3f}us")
    return ",".join(steps)


def onewire_transactions(ev):
    """Each reset's transaction: its slots as (kind, value) bits, grouped later
    by the commands they carry."""
    tx, cur = [], None
    for e in ev:
        if e[0] == "reset":
            cur = {"at": e[1], "presence": None, "slots": []}
            tx.append(cur)
        elif e[0] == "presence" and cur is not None:
            cur["presence"] = e
        elif cur is not None:
            cur["slots"].append(e[0])
    return tx


def _byte(bits):
    return sum(b << k for k, b in enumerate(bits))


def onewire_decode(tx):
    """What each transaction says, by the DS18B20's commands: its ROM command,
    the device it selects (Match ROM), its function and the bytes read. The
    ROM search's triplets are kept as they are."""
    out = []
    for t in tx:
        sl = t["slots"]
        bit = lambda k: 0 if sl[k] in ("w0", "r0") else 1  # noqa: E731
        d = {"presence": t["presence"] is not None, "rom_cmd": None, "rom": None, "fn": None, "read": []}
        k = 0
        if len(sl) >= 8:
            d["rom_cmd"] = _byte([bit(i) for i in range(8)])
            k = 8
            if d["rom_cmd"] == 0x55 and len(sl) >= k + 64:
                d["rom"] = _byte([bit(i) for i in range(k, k + 64)])
                k += 64
            elif d["rom_cmd"] in (0xF0, 0xEC):
                k = len(sl)  # the search: triplets to the end
            if len(sl) >= k + 8:
                d["fn"] = _byte([bit(i) for i in range(k, k + 8)])
                k += 8
            reads = [bit(i) for i in range(k, len(sl)) if sl[i] in ("r0", "r1")]
            d["read"] = [_byte(reads[i:i + 8]) for i in range(0, len(reads) - 7, 8)]
        d["read_bits"] = "".join("0" if s == "r0" else "1" for s in sl if s in ("r0", "r1"))
        out.append(d)
    return out


def ds18b20_temperature(scratch):
    raw = scratch[0] | scratch[1] << 8
    if raw & 0x8000:
        raw -= 0x10000
    return raw / 16


# ---- running a model ------------------------------------------------------------------


ALIGN_LEAD = 12.0  # s: long enough a part is idle for any start it will ever answer


def run_bench(embsim, name, components, net_pins, inputs, until_ps, lead_ps, work):
    """A pattern generator as the host on one open-drain net with a pull-up and the
    parts; the net's edges in the capture's time base (the lead taken off)."""
    proj = {
        "schema_version": 1, "kind": "project", "name": name, "policy": "functional",
        "components": [{"id": "PU", "component": "emblink/pull_up"}] + components,
        "nets": {"DATA": ["HOST.OUT0", "PU.1"] + net_pins},
        "probes": [{"name": "data", "target": "net:DATA", "kind": "digital"}],
    }
    scen = {"schema_version": 1, "kind": "scenario", "name": name, "until": f"{until_ps}ps", "inputs": inputs}
    p, s, rec = work / f"{name}.json", work / f"{name}-s.json", work / f"{name}-rec.json"
    p.write_text(json.dumps(proj), encoding="utf-8")
    s.write_text(json.dumps(scen), encoding="utf-8")
    run = subprocess.run([embsim, "run", str(p), "--scenario", str(s), "--record", str(rec), "--quiet"],
                         capture_output=True, text=True, errors="replace", timeout=900)
    if run.returncode not in (0, 1) or not rec.exists():
        raise RuntimeError(f"{name}: embsim failed ({run.returncode}): {run.stderr[-600:]}")
    obs = json.loads(rec.read_text(encoding="utf-8"))["observations"]
    out, last = [], None
    for o in obs:
        if o.get("kind") != "probe":
            continue
        t, v = max(0, int(o["at_ps"]) - lead_ps), 1 if o["value"] == "1" else 0
        if v != last:
            out.append((t, v))
            last = v
    return out


def host(waveform):
    return {"id": "HOST", "component": "emblink/pattern_generator",
            "params": {"waveform": waveform, "idle": 1, "drive": "open_drain"}}


def check_phases(real, sim, tol, problems):
    for name, rv in real.items():
        sv = sim.get(name)
        limit = tol.get(name, tol.get("default", 2.0))
        if rv is None:
            continue
        if sv is None:
            problems.append(f"{name}: real {rv:.1f} us, the model has none")
        elif abs(rv - sv) > limit:
            problems.append(f"{name}: real {rv:.1f} us, model {sv:.1f} us (more than {limit} us apart)")


# ---- per protocol: replay, decode, compare -------------------------------------------------


def am230x_simulate(embsim, case, reads, work, lead_s):
    lead = int(lead_s * 1e12)
    # past the last start's release and its answer (a frame takes about 5 ms)
    until = (reads[-1]["at"] + reads[-1]["start"] + lead + 50_000 * US) if reads else lead
    h, t = next((am230x_values(r["bytes"]) for r in reads if r["answered"]), (50.0, 25.0))
    inputs = [{"at": "0", "target": "DUT", "input": "humidity", "value": h},
              {"at": "0", "target": "DUT", "input": "temperature", "value": t}]
    comps = [host(am230x_waveform(reads, lead, until)), {"id": "DUT", "component": case["part"]}]
    return run_bench(embsim, case["name"], comps, [f"DUT.{case['pin']}"], inputs, until, lead, work)


def am230x_case(embsim, case, edges, work):
    real_reads = am230x_reads(edges)
    k = next((i for i, r in enumerate(real_reads) if r["answered"]), None)
    first, offset = 1, 0
    if k is None:  # never answered: no history to align on
        sim_reads = am230x_reads(am230x_simulate(embsim, case, real_reads, work, case.get("lead_s", 1.2)))
    else:  # from the real part's first answer, after a long idle
        tail = [dict(r, at=r["at"] - real_reads[k]["at"]) for r in real_reads[k:]]
        sim_reads = am230x_reads(am230x_simulate(embsim, case, tail, work, ALIGN_LEAD))
        real_reads, first, offset = real_reads[k:], 0, k
    problems = []
    n = min(len(real_reads), len(sim_reads))
    if len(real_reads) != len(sim_reads):
        problems.append(f"{len(real_reads)} host starts in the capture, {len(sim_reads)} in the simulation")
    for i in range(first, n):
        a, b = real_reads[i]["answered"], sim_reads[i]["answered"]
        if a != b:
            problems.append(f"read {i + 1 + offset} (start {real_reads[i]['start'] / US:.0f} us): "
                            f"the real part {'answered' if a else 'did not answer'}, the model "
                            f"{'answered' if b else 'did not'}")
    real, sim = am230x_phases(real_reads), am230x_phases(sim_reads)
    if any(r["answered"] for r in sim_reads) or not any(r["answered"] for r in real_reads):
        check_phases(real, sim, case.get("tolerance_us", {}), problems)
    elif real:
        problems.append("the model answered none of the reads the real part answered")
    for i, r in enumerate(sim_reads):
        if r["answered"] and not checksum_ok(r["bytes"]):
            problems.append(f"simulated read {i + 1}: checksum {r['bytes']} does not hold")
    return {"real": real, "model": sim, "problems": problems,
            "summary": f"{n} reads, answered {sum(r['answered'] for r in real_reads)} real / "
                       f"{sum(r['answered'] for r in sim_reads)} model"}


def onewire_phases(ev):
    pres = [e for e in ev if e[0] == "presence"]
    r0 = [e[2] for e in ev if e[0] == "r0"]
    mean = lambda xs: statistics.mean(xs) / US if xs else None  # noqa: E731
    return {"presence_wait": mean([e[3] for e in pres]), "presence_low": mean([e[2] for e in pres]),
            "read_0_low": mean(r0)}


def onewire_case(embsim, case, edges, work):
    """Every bit the parts sent, compared one for one past the transactions that
    show what they held before the capture (a scratchpad read before the first
    Convert T: the last conversion and the EEPROM's configuration are from
    before), and the parts' timing."""
    ev = onewire_events(edges)
    real_tx = onewire_decode(onewire_transactions(ev))
    # each part's temperature, from its first scratchpad read after a conversion
    temps, converted, selected = {}, False, None
    for d in real_tx:
        if d["fn"] == 0x44:
            converted = True
        if d["rom_cmd"] == 0x55:
            selected = d["rom"]
        if d["fn"] == 0xBE and converted and selected is not None and len(d["read"]) >= 2:
            temps.setdefault(selected, ds18b20_temperature(d["read"]))
    lead = int(case.get("lead_s", 0.01) * 1e12)
    until = (ev[-1][1] + ev[-1][2] + lead + 10_000 * US) if ev else lead
    comps, pins, inputs = [host(onewire_waveform(ev, lead, until))], [], []
    for i, dev in enumerate(case["devices"]):
        rom = int(dev["rom"], 16)
        did = f"DUT{i + 1}"
        comps.append({"id": did, "component": case["part"], "params": {"serial": (rom >> 8) & 0xFFFFFFFFFFFF}})
        pins.append(f"{did}.{case['pin']}")
        if rom in temps:
            inputs.append({"at": "0", "target": did, "input": "temperature", "value": temps[rom]})
    sim_ev = onewire_events(run_bench(embsim, case["name"], comps, pins, inputs, until, lead, work))
    sim_tx = onewire_decode(onewire_transactions(sim_ev))
    problems = []
    if len(real_tx) != len(sim_tx):
        problems.append(f"{len(real_tx)} transactions in the capture, {len(sim_tx)} in the simulation")
    converted, compared, differing = False, 0, 0
    for i, (a, b) in enumerate(zip(real_tx, sim_tx)):
        if a["fn"] == 0x44:
            converted = True
        history = a["fn"] == 0xBE and not converted
        if a["presence"] != b["presence"]:
            problems.append(f"transaction {i + 1}: presence {a['presence']} real, {b['presence']} model")
        if history:
            continue
        compared += len(a["read_bits"])
        if a["read_bits"] != b["read_bits"]:
            differing += 1
            if differing <= 5:
                problems.append(f"transaction {i + 1} (ROM 0x{a['rom_cmd'] or 0:02X}, function "
                                f"0x{a['fn'] or 0:02X}): the parts sent {a['read_bits'] or '-'}, the models "
                                f"{b['read_bits'] or '-'}")
    real, sim = onewire_phases(ev), onewire_phases(sim_ev)
    check_phases(real, sim, case.get("tolerance_us", {}), problems)
    return {"real": real, "model": sim, "problems": problems, "temperatures": {hex(k): v for k, v in temps.items()},
            "summary": f"{len(real_tx)} transactions, {compared} bits the parts sent compared"}


PROTOCOLS = {"am230x": am230x_case, "onewire": onewire_case}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--embsim", default=str(ROOT / "build" / "dev" / "bin" / "embsim"))
    ap.add_argument("--case", action="append")
    ap.add_argument("--report")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()
    cases = []
    for f in sorted((ROOT / "tests" / "captures").glob("*/cases.json")):
        for c in json.loads(f.read_text(encoding="utf-8"))["cases"]:
            c["dir"] = f.parent
            if not args.case or c["name"] in args.case:
                cases.append(c)
    report, failed = {}, 0
    with tempfile.TemporaryDirectory(prefix="embsim-captures-") as tmp:
        for c in cases:
            cap = read_sr(c["dir"] / c["capture"])
            res = PROTOCOLS[c["protocol"]](args.embsim, c, cap.edges(c.get("channel", 0)), Path(tmp))
            report[c["name"]] = res
            ok = not res["problems"]
            failed += not ok
            print(f"{'ok  ' if ok else 'FAIL'}  {c['name']}: {res['summary']}")
            if args.verbose or not ok:
                for name, rv in res["real"].items():
                    sv = res["model"].get(name)
                    print(f"        {name:24s} real {rv if rv is None else f'{rv:6.1f}'} us   model "
                          f"{sv if sv is None else f'{sv:6.1f}'} us")
            for p in res["problems"]:
                print(f"        {p}")
    if args.report:
        Path(args.report).write_text(json.dumps(report, indent=2, default=str), encoding="utf-8")
    print(f"{len(cases) - failed}/{len(cases)} capture(s) match")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
