<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Junior Deogracias -->
# Protocol conformance

SPEC-008 §10: a method is part of the protocol only when it has a
request/response fixture here. `run_conformance.py` replays the fixtures
against a real `embsim` over both transports and checks that

1. each response matches its expected shape (`cases/*.json`), and
2. the stdio and WebSocket transports answer identically.

## Running

```
python3 tests/protocol/run_conformance.py --embsim build/dev/bin/embsim \
    --project examples/button-led/project.json --port 18765
```

The script starts `embsim sidecar PROJECT` (stdio) and `embsim serve PROJECT
--port PORT` (WebSocket, loopback only), runs every case in order on each,
prints a summary and exits 0 when everything passed, 1 otherwise (2 for a
usage problem, such as a missing binary). `--transport stdio|ws` restricts
the run; `--cases DIR` points at another fixture directory; `--verbose`
prints each exchange.

## Fixture format

One JSON file per case, numbered so that they run in order:

```json
{
  "name": "control.advance by a duration",
  "method": "control.advance",
  "params": {"by": "10ms"},
  "expect": {"result": {"now_ps": "<any>"}}
}
```

- `expect.result` is matched structurally: every key listed must be present
  with an equal value; extra keys in the actual result are allowed (the
  protocol only adds fields within a major version).
- `"<any>"` matches any value; `"<string>"`, `"<number>"`, `"<bool>"`,
  `"<array>"`, `"<object>"` match any value of that type; `"<ps>"` matches a
  decimal picosecond string (SPEC-008 §4).
- `expect.error` matches a JSON-RPC error object the same way (`code`,
  `message`, `data`).
- `volatile` lists result keys whose values may legitimately differ between
  the two transports (they are still matched against `expect`).
- `skip_compare: true` excludes the case from the cross-transport comparison.

The method names and parameters follow SPEC-008 §5 exactly; `input.set`
targets the push button `SW1` of the SPEC-007 §1 example project, so point
`--project` at a project with that component (or adapt `cases/`).
