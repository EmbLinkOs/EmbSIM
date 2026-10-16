# SPEC-008 - Session Protocol

**Status:** Accepted 2026-10-15. **Builds on:** 05 §1, §2; ADR-007.
**Requirements:** `docs/requirements/PROTO.md`, `docs/requirements/CLI.md`.

---

## 1. Transport and framing

JSON-RPC 2.0. Requests, responses and notifications are UTF-8 JSON objects.

| Transport | Framing | Binary streams |
|---|---|---|
| stdio (`embsim sidecar`) | one JSON object per line (`\n`-terminated, no embedded newlines) | notifications carry `base64` payloads unless the client negotiated a side pipe (`streams: "pipe"`) |
| WebSocket (`embsim serve`) | one JSON object per text frame | binary frames: 16-byte header (`stream_id u32`, `seq u32`, `first_time_ps u64`) then samples |
| in-process (`es_session_rpc`) | one request string, one response string | `es_session_probe_read` |

Batch requests are not supported. Every request has an `id`; the server
replies in order per transport but may interleave notifications.

The WebSocket endpoint listens on loopback and serves one client at a
time. A browser connection is accepted when its `Origin` is the server's
own (`http://127.0.0.1:PORT`, `http://localhost:PORT`) or one the host
allowed (`embsim serve --allow-origin ORIGIN`, `es_serve_allow_origin`):
the desktop shell allows its webview's origin (`tauri://localhost`,
`http://tauri.localhost`), so a page on another site cannot drive the
engine. Clients that send no `Origin` (scripts, tools) are accepted.

## 2. Versioning and capabilities

`session.open` (or the implicit first call on the in-process transport)
returns `{"protocol": "1.0", "engine": "0.1.0", "abi": 1, "capabilities":
["analog", "lua", "gdb", "debug.dwarf", "streams.binary", "toolchain"]}`.
The client sends `{"client": "workbench/0.1", "protocol": "1.0", "streams":
"binary|base64|pipe"}`. A major protocol version mismatch is an error
`-32001`.

## 3. Errors

JSON-RPC error objects: `code` from the standard range for protocol faults
(`-32600` invalid request, `-32601` unknown method, `-32602` invalid params)
and from `-32000` down for engine statuses (`-32000 - es_status`), `message`
from `es_strerror`, `data` holding the diagnostic record (SPEC-001 §7) when
one was produced.

## 4. Time representation

Every time value is a decimal string of picoseconds. Durations in requests
may use unit strings as in SPEC-007 §2 (`"10ms"`); responses always use
picoseconds.

## 5. Methods

Notation: `method(params) -> result`. Params and results are objects.

### session
- `session.open({client, protocol, streams}) -> {protocol, engine, abi, capabilities}`
- `session.close({}) -> {}`
- `session.info({}) -> {project, policy, now_ps, running, speed, boards: [...], islands, limits, timing_tiers}`
- `session.policy({policy}) -> {policy}` (allowed only while paused; recomputes islands)

### project
- `project.load({path} | {json, base_dir}) -> {name, diagnostics: [...]}`
- `project.get({}) -> {project}` (the canonical JSON, with resolved library versions)
- `project.save({path?}) -> {path}`: the project's JSON (two-space indent) to
  `path`, relative to the project's folder, or to the file it was loaded
  from; `ES_EIO` when it cannot be written
- `project.validate({path} | {json}) -> {ok, diagnostics}`
- `project.transaction({op: "begin"}) -> {tx}`;
  `project.transaction({op: "edit", tx, edits: [{add_component}, {remove_component}, {set_param}, {connect, net, pin}, {disconnect, pin}, {rename_net}, {add_board}, {set_layout}]}) -> {}`;
  `project.transaction({op: "commit", tx}) -> {applied, restarted, diagnostics}`;
  `project.transaction({op: "abort", tx}) -> {}`.
  One transaction is open at a time (a second `begin` is `ES_ESTATE`). Edits
  collect on a copy of the project; an edit that cannot apply (an unknown
  instance, a used id) fails with `-32602` and leaves the copy as it was
  before that edit. A commit of `set_layout` edits only (placement, wires,
  labels) replaces the project's JSON and the run goes on. A commit that
  changes the circuit loads the edited project in a scratch session first:
  if it does not load, the commit fails with `ES_ESCHEMA`, the errors are
  raised as diagnostics and the project stays as it was; otherwise the
  session is rebuilt from it and restarts at time 0 (`restarted: true`).
  `remove_board {id}` is accepted beside the edits above. Edits made while
  running pause the run when they restart it.
- `project.topology({}) -> {nets: [{name, island, pins: [...], state}], islands: [{id, kind, nets}]}`

### control
- `control.run({speed?}) -> {running: true}`: runs until paused, a fatal
  diagnostic, a breakpoint, or `until` when given (`control.run({until})`);
  `speed` 1.0 means real time, 0 means unbounded.
- `control.pause({}) -> {now_ps}`
- `control.step({mode: "event" | "instruction" | "delta", board?}) -> {now_ps}`
- `control.advance({until} | {by}) -> {now_ps}` (synchronous; the server may stream observations while advancing)
- `control.reset({reload_images?: true}) -> {}`
- `control.speed({speed}) -> {speed}`

### input
- `input.set({target, input, value, at?}) -> {at_ps, seq}`: `at` defaults to `now`; a past time is an error.
- `input.press({target})`, `input.release({target})`: sugar for boolean inputs named `pressed`.
- `input.serial({target, bytes: base64 | text}) -> {at_ps, seq}`
- `input.list({}) -> {inputs: [{target, input, type, value, min, max}]}`

### probe and waveform
- `probe.add({target, kind, rate_hz?, decode?: {protocol, params}}) -> {probe, stream?}`
- `probe.remove({probe}) -> {}`
- `probe.list({}) -> {probes: [...]}`
- `probe.read({probe, from_ps?, to_ps?, max?}) -> {samples: [[time_ps, value], ...], gap?}`
- `waveform.subscribe({probes: [...], max_rate_hz?}) -> {stream}`; samples arrive as `waveform.samples` notifications (`base64`) or binary frames; `waveform.unsubscribe({stream})`.
- Sample encodings: digital `u8` (`0,1,2=Z,3=X`), analog `f32`, serial bytes, value `f64`; each frame is monotonic in time; a `gap` marker reports dropped frames.

### observe
- `observe.snapshot({include?: ["nets", "components", "boards", "outputs", "probes"]}) -> {...}`: the full state at `now` for simple clients and the CLI.
- `observe.subscribe({what: ["nets", "outputs", "diagnostics", "clock"], max_rate_hz}) -> {}`; delivers `observe.update` notifications with only changed entries.

### debug (per board MCU)
- `debug.breakpoint({board, op: "add"|"remove", kind: "code"|"read"|"write"|"access", address|symbol|file+line, length?}) -> {id}`
- `debug.registers({board}) -> {regs: [{name, value}]}`; `debug.set_register({board, name, value})`
- `debug.memory({board, address, length}) -> {base64}`; `debug.write_memory({board, address, base64})`; the address is a program address, as the core's `phys` hook maps it to the bus (a MIPS core's kseg0 and kseg1), and so for the GDB server's
- `debug.where({board}) -> {pc, function, file, line, frames: [...], ram, core?}` (DWARF); `ram` is the part's main RAM (its largest RAM region), where a memory view starts; `core`, on a part with several cores, is the one the program counter and the registers are (the one that runs next)
- `debug.variables({board, frame?}) -> {...}`
- `debug.disassemble({board, address, count}) -> {insns: [...]}`
- `debug.peripherals({board}) -> {peripherals: [{name, base, model, clock, reset, registers: [{name, offset, value, fields: [...]}]}]}`; `clock` false while the peripheral's clock is off, `reset` true while it is held in reset (the RP2040's RESETS)
- `debug.gdb({board, op: "start"|"stop", port}) -> {port}`
- `control.step({mode: "instruction", board})` and `control.run` honour breakpoints; a stop is reported by `debug.stopped` notification.

### catalog
- `catalog.list({kind?, category?, query?}) -> {items: [{id, version, name, category, fidelity_tiers, broken?}]}`
- `catalog.get({id, version?}) -> {description, files: {symbol, artwork}}`
- `catalog.validate({path}) -> {ok, diagnostics}`
- `catalog.libraries({}) -> {libraries: [{path, name, version, items}]}`

### record
- `record.start({path?}) -> {}`; `record.stop({}) -> {path, inputs, hash}`
- `record.replay({path}) -> {ok, divergence?: {at_ps, expected, observed}}`

### toolchain
- `toolchain.detect({}) -> {profiles: [{name, found, version, path}]}`
- `toolchain.build({board, profile?}) -> {ok, log, image}`

### scenario
- `scenario.run({path} | {scenario}) -> {report}` (SPEC-007 §2; the session is reset first)

## 6. Notifications (server to client)

`diagnostics({records: [...]})`, `observe.update({now_ps, nets?, outputs?,
boards?})`, `waveform.samples({stream, seq, first_time_ps, encoding,
data})`, `control.state({running, now_ps, reason?})` (on run, pause, stop,
end), `debug.stopped({board, reason, pc, breakpoint?})`, `serial.data({target,
base64})`, `project.changed({tx, summary})`, `record.input({at_ps, seq,
target, input})`.

## 7. Threading and ordering guarantees

Requests are executed in order on the session; a `control.run` returns
immediately and the run proceeds on the server's run loop in slices
(SPEC-002 §8) between which queued requests are served, so an `input.set`
during a run is applied at the virtual time of the next slice boundary at
the latest, and never reorders with other inputs. Notifications for one
stream are in order; different streams are independent.

## 8. Reports

`scenario.run`, `record.replay` and `embsim run --json` produce one JSON
report: `{engine, protocol, project: {name, hash}, images: [{board, path,
hash}], policy, until_ps, ended: {reason, now_ps}, assertions: [{id, ok,
observed, expected, at_ps}], diagnostics: [...], statistics: {events,
instructions: {board: n}, analog_steps, wall_time_s}}`.

## 9. CLI exit codes

`0` success and all assertions passed; `1` an assertion failed; `2` usage or
load error; `3` a fatal diagnostic stopped the run; `4` replay divergence;
`5` toolchain build failed.

## 10. Conformance

`tests/protocol/` holds request and expected-response pairs and a Python
client (`tools/embsim_client`) that exercises every method against the
in-process and WebSocket transports; a method not covered there is not part
of the protocol.
