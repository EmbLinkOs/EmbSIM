# SPEC-012 - Record, Replay and Verification

**Status:** Accepted in outline 2026-10-15; completed before M1 (recording
of inputs is implemented in M0); revised 2026-10-16 to what M5 implements:
observations, replay with its divergence report, recordings to scenarios.
**Builds on:** 04 §8; ADR-012. **Schema:** `schemas/recording.schema.json`.
**Requirements:** `docs/requirements/VER.md`.

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
Observations are the discrete checkpoints replay compares, in time order:
interrupt entries per core (`irq`: the board, the core on a part with
several, the core's own number for the exception: `esk_mach_host.exc`),
digital probe transitions (`probe`, from the level at reset), each serial
probe's text at the end (`serial`), each warning, error and fatal
diagnostic (`diagnostic`) and the run's end (`end`). Analog probes are not
observed. A recording keeps at most 200000 observations (`truncated` past
them). A run with `--record` starts from reset, as a scenario does; a
recording made with `record.start` in a running session carries `from_ps`,
and replay compares from there. `probes` lists the probes the run added
beside the project's (`--probe`), which replay adds again.

## 2. Replay

`embsim replay` and `record.replay` load the recorded project (refusing on
a project or image hash mismatch, exit 2, unless `--allow-drift`, which
reports the differences as `drift`), reset it, apply the inputs at their
times through the command path, run to the recorded end, and compare the
observations in order; the first mismatch is reported with both
observations (`divergence: {index, expected, observed}`), exit code 4. A
replay never accepts live input.

## 3. From recording to scenario

`embsim record to-scenario` emits a SPEC-007 scenario from the inputs and
turns selected observations into assertions: each digital probe's level at
the end and its edges over the run (`--assert probes`), each serial probe's
text (`--assert serial`), both by default.

## 4. Verification in CI

`embsim run PROJECT --scenario S --json --record out.rec`
is the CI contract: exit code and report (SPEC-008 §8). Reports and
recordings are artifacts; `tools/verify/compare_reports.py` diffs two
reports (for a firmware change) ignoring wall time. `--coverage FILE
--coverage-format lcov` and `--profile FILE --profile-format collapsed`
add the firmware's coverage and profile (SPEC-009 §12) as artifacts a CI's
coverage service and a flame graph read; the report carries their
summaries. `embsim run --target BOARD --image FILE --until-text TEXT
--console` runs a test image on a catalog board with no project, for a test
runner that reads the console (docs/user/ci.md).

## 5. Determinism tests

`tests/protocol/test_replay.py` (`record_replay` in CTest) records each
example's first scenario twice, in two processes, and requires the two
recordings' inputs and observations to be equal; it replays each, and checks
a changed observation's divergence report, a changed image's refusal, a
recording turned into a scenario, and `record.replay` over the protocol.
Full event traces (`--trace-events`) and analog comparisons with tolerances
are not implemented.
