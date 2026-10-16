# SPEC-012 - Record, Replay and Verification

**Status:** Accepted in outline 2026-10-15; completed before M1 (recording
of inputs is implemented in M0). **Builds on:** 04 §8; ADR-012.
**Schema:** `schemas/recording.schema.json`. **Requirements:**
`docs/requirements/VER.md`.

---

## 1. Recording

```json
{
  "schema_version": 1, "kind": "recording",
  "engine": {"version": "0.1.0", "abi": 1, "platform": "linux-x86_64", "build": "gcc-13"},
  "project": {"path": "button.json", "hash": "sha256:..."},
  "catalog": [{"id": "emblink/led", "version": "1.0.0", "hash": "sha256:..."}],
  "images": [{"board": "B1", "path": "build/fw.elf", "hash": "sha256:..."}],
  "policy": "electrical", "settings": {...},
  "inputs": [{"at_ps": "10000000000", "seq": 1, "target": "SW1", "input": "pressed", "value": true}],
  "observations": [
    {"at_ps": "10000012345", "kind": "irq", "board": "B1", "n": 22},
    {"at_ps": "10000300000", "kind": "probe", "probe": "btn", "value": 0}
  ],
  "end": {"at_ps": "60000000000", "reason": "until", "statistics": {...}}
}
```

Every `EXTERNAL` event is an input: user actions, scenario inputs, serial
bytes, debugger writes, topology transactions (recorded as their edit list).
Observations are the discrete checkpoints replay compares: interrupt
entries per core, digital probe transitions, run end; analog probes are
compared with tolerances when present.

## 2. Replay

`record.replay` loads the recorded project (refusing on hash mismatch unless
`--allow-drift`, which reports differences), applies inputs at their times
and sequences through the command path, and compares observations in
order; the first mismatch is reported with both values and times, exit code
4. A replay never accepts live input.

## 3. From recording to scenario

`embsim record to-scenario` emits a SPEC-007 scenario from the inputs and
turns selected observations into assertions (`--assert probes`).

## 4. Verification in CI

`embsim run PROJECT --scenario S --json --policy strict --record out.rec`
is the CI contract: exit code and report (SPEC-008 §8). Reports and
recordings are artifacts; `tools/verify/compare_reports.py` diffs two
reports (for a firmware change) ignoring wall time.

## 5. Determinism tests

`tests/determinism/` runs each example twice in one process and in two
processes and compares full event traces (`--trace-events`) byte for byte;
the analog subset compares with tolerances.
