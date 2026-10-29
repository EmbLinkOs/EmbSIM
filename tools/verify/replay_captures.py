#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Real parts' captures replayed into their models (01 §8: the outside of a
part matches the real one's).

Each case in tests/captures/*/cases.json names a logic capture of a real
part on its bus (a sigrok session file) and the catalog part that models it.
The host's half of the capture is played into the model by a pattern
generator at the same times; the model's answers are recorded; both the
real and the simulated answers are decoded by the protocol's rules and
compared:

  - whether each read was answered. What the part did before the capture
    began is not known, and it decides the first answers (a part still
    measuring, or idle long enough to hear a weak start), so the replay
    begins at the first read the real part answered, after a long idle, and
    every read from there is compared; a capture the part never answers is
    compared from its second read;
  - each timed phase of the answers (the response, the bits' low and high
    times, the first bit of each byte, the closing low), the mean over all
    answered reads, within the case's tolerance;
  - that every simulated frame's checksum holds.

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


# ---- running the model ----------------------------------------------------------------


ALIGN_LEAD = 12.0  # s: long enough a part is idle for any start it will ever answer


def simulate(embsim, case, reads, work, lead_s=None):
    """The model's answers to the capture's host starts: its edges on the wire,
    in the capture's time base."""
    lead = int((lead_s if lead_s is not None else case.get("lead_s", 1.2)) * 1e12)
    # past the last start's release and its answer (a frame takes about 5 ms)
    until = (reads[-1]["at"] + reads[-1]["start"] + lead + 50_000 * US) if reads else lead
    real_values = next((am230x_values(r["bytes"]) for r in reads if r["answered"]), (50.0, 25.0))
    proj = {
        "schema_version": 1, "kind": "project", "name": case["name"], "policy": "functional",
        "components": [
            {"id": "HOST", "component": "emblink/pattern_generator",
             "params": {"waveform": am230x_waveform(reads, lead, until), "idle": 1, "drive": "open_drain"}},
            {"id": "PU", "component": "emblink/pull_up"},
            {"id": "DUT", "component": case["part"]},
        ],
        "nets": {"DATA": ["HOST.OUT0", "PU.1", f"DUT.{case['pin']}"]},
        "probes": [{"name": "data", "target": "net:DATA", "kind": "digital"}],
    }
    scen = {
        "schema_version": 1, "kind": "scenario", "name": case["name"], "until": f"{until}ps",
        "inputs": [{"at": "0", "target": "DUT", "input": "humidity", "value": real_values[0]},
                   {"at": "0", "target": "DUT", "input": "temperature", "value": real_values[1]}],
    }
    p, s, rec = work / f"{case['name']}.json", work / f"{case['name']}-s.json", work / f"{case['name']}-rec.json"
    p.write_text(json.dumps(proj), encoding="utf-8")
    s.write_text(json.dumps(scen), encoding="utf-8")
    run = subprocess.run([embsim, "run", str(p), "--scenario", str(s), "--record", str(rec), "--quiet"],
                         capture_output=True, text=True, errors="replace", timeout=900)
    if run.returncode not in (0, 1) or not rec.exists():
        raise RuntimeError(f"{case['name']}: embsim failed ({run.returncode}): {run.stderr[-600:]}")
    obs = json.loads(rec.read_text(encoding="utf-8"))["observations"]
    edges = [(int(o["at_ps"]) - lead, 1 if o["value"] == "1" else 0) for o in obs if o.get("kind") == "probe"]
    # the real capture's time base: the lead taken off, the wire released before it
    edges = [(max(0, t), v) for t, v in edges]
    out, last = [], None
    for t, v in edges:
        if v != last:
            out.append((t, v))
            last = v
    return out


def compare(case, real_reads, sim_reads, first=1, offset=0):
    """`first`: the first read whose answer is compared; `offset`: the number of
    the capture's reads the replay began after (for the messages)."""
    tol = case.get("tolerance_us", {})
    problems = []
    n = min(len(real_reads), len(sim_reads))
    if len(real_reads) != len(sim_reads):
        problems.append(f"{len(real_reads)} host starts in the capture, {len(sim_reads)} in the simulation")
    for k in range(first, n):
        a, b = real_reads[k]["answered"], sim_reads[k]["answered"]
        if a != b:
            problems.append(f"read {k + 1 + offset} (start {real_reads[k]['start'] / US:.0f} us): "
                            f"the real part {'answered' if a else 'did not answer'}, the model "
                            f"{'answered' if b else 'did not'}")
    real, sim = am230x_phases(real_reads), am230x_phases(sim_reads)
    for name, rv in real.items():
        sv = sim.get(name)
        limit = tol.get(name, tol.get("default", 2.0))
        if rv is None:
            continue
        if sv is None:
            if real_reads and any(r["answered"] for r in sim_reads):
                problems.append(f"{name}: real {rv:.1f} us, the model has none")
            continue
        if abs(rv - sv) > limit:
            problems.append(f"{name}: real {rv:.1f} us, model {sv:.1f} us (more than {limit} us apart)")
    for k, r in enumerate(sim_reads):
        if r["answered"] and not checksum_ok(r["bytes"]):
            problems.append(f"simulated read {k + 1}: checksum {r['bytes']} does not hold")
    return {"real": real, "model": sim, "reads": n,
            "answered_real": sum(r["answered"] for r in real_reads),
            "answered_model": sum(r["answered"] for r in sim_reads), "problems": problems}


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
            real_reads = am230x_reads(cap.edges(c.get("channel", 0)))
            k = next((i for i, r in enumerate(real_reads) if r["answered"]), None)
            if k is None:  # never answered: no history to align on
                sim_reads = am230x_reads(simulate(args.embsim, c, real_reads, Path(tmp)))
                res = compare(c, real_reads, sim_reads)
            else:  # from the real part's first answer, after a long idle
                tail = [dict(r, at=r["at"] - real_reads[k]["at"]) for r in real_reads[k:]]
                sim_reads = am230x_reads(simulate(args.embsim, c, tail, Path(tmp), ALIGN_LEAD))
                res = compare(c, real_reads[k:], sim_reads, first=0, offset=k)
                res["aligned_at_read"] = k + 1
            report[c["name"]] = res
            ok = not res["problems"]
            failed += not ok
            print(f"{'ok  ' if ok else 'FAIL'}  {c['name']}: {res['reads']} reads, answered "
                  f"{res['answered_real']} real / {res['answered_model']} model")
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
