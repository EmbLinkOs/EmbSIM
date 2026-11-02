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

The WebSocket endpoint listens on loopback and serves one session client at a
time. Static HTTP requests are serviced between that client's protocol polls,
so browser reloads and asset requests do not wait for its WebSocket to close.
Auxiliary connections cannot attach a second session (HTTP 409 for an otherwise
allowed upgrade). They retain Host and Origin checks and have a 250 ms request-head
deadline; ordinary initial request heads retain their 10-second deadline. A browser connection is accepted when its `Origin` is the server's
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
  labels, buses, groups and placement locks) replaces the project's JSON and the run goes on. A commit that
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
- A board's serial input (`input.set` on `B1`, input `serial` or `tx_text`) takes text or `{"base64": ...}` for any byte, NUL among them; a host port's bytes are recorded in that form when they are not plain ASCII.
- `input.list({}) -> {inputs: [{target, input, type, value, min, max, world?, source?}]}`: an input bound to the world names its field (`world`) and, while it reads it, `source: "world"`.
- Targets `world` (the input a field's name, the value a number or null to undeclare it) and `world.BODY` (the input `body`, the value `{shape?, at, size}` or null to remove it) change the world (SPEC-003 §8).

### world
- `world.get({}) -> {fields: {NAME: {value, unit, set}}, bodies: [{id, shape, at, size}]}`: the world as it is now.

### port (a board's serial port on the host, ADR-031 and ADR-032)
- `port.open({target}) -> {target, path, connected, baud, sketch_baud, to_host, from_host}`: a board's console (`B1`) or a terminal part (`VT1`) becomes a pseudo-terminal on the host at `path`, which any serial program opens; opening it again gives the same port. `connected` is whether a program has it open, `baud` the rate that program set on the line, `sketch_baud` the console's rate as the firmware set it. Error `-32008` where the host has no pseudo-terminals (Windows, Q16) and `-32005` for a target that is neither.
- `port.close({target}) -> {}`, `port.list({}) -> {ports: [...]}`.
- While the session runs, the port is read and written at each run slice:
  - The program's bytes become inputs at the session's time (`serial` on a board, `tx_text` on a part), recorded so that a replay needs no host.
  - The console's bytes go to the program. They are dropped while no program has the port open, as a bridge drops what the host does not read.
  - When `baud` and `sketch_baud` differ by more than 3 %, both directions are what a UART at one rate reads from frames sent at the other.
  - A board whose `console.bridge.dtr_reset` names a component (SPEC-006) has that component's `pressed` held for `ms` when a program opens the port. This is DTR's reset through the Uno's 100 nF capacitor, also a recorded input.
- `embsim serve PROJECT --serial B1` opens the port when the session starts and prints its path.

### probe and waveform
- `probe.add({target, kind, rate_hz?, decode?: {protocol, params}}) -> {probe, stream?}`
- `probe.remove({probe}) -> {}`
- `probe.list({}) -> {probes: [...]}`
  Probe entries include `unit` from the declared output/native electrical
  quantity and actual `rate_hz` (zero for an event-driven probe).
- `probe.read({probe, from_ps?, to_ps?, max?}) -> {samples: [[time_ps, value], ...], gap?}`
- `instrument.read({part, since?, max?}) -> {kind: "logic", state, sample_rate, start_ps, period_ps, trigger, end, channels, first, since, next, full, transitions: [[sample, bits], ...]}`:
  a logic analyzer part's capture (`docs/engineering/bench-instruments.md`)
  from sequence number `since`; `sample` counts from the capture's start
  (`start_ps`, `period_ps` apart), `bits` holds channel n in bit n, and the
  first transition is the inputs at the start. `next` is the `since` of the
  following read; `trigger` and `end` are sample numbers, -1 until known.
  Waiting for a trigger, only the pre-trigger window is held: `first` is
  the sequence number of its first record, which moves on (that record's
  sample moving to the window's start), and a reader starts over from
  `first` when it changes.
- `instrument.read({part, since?, max?}) -> {kind: "power", state, mode, sample_rate, start_ps, period_ps, first, since, next, samples: [amperes, ...]}`:
  a power profiler's measurement core (a PPK2's `METER`): one sample per
  `period_ps` (10 µs) from `start_ps`, each the current averaged over its
  window at its range's resolution; `first` is the oldest still held.
- `instrument.read({part, since?}) -> {kind: "scope", status, seq, trigger_ps?, x0_ps?, dx_ps?, sample_rate?, memory_points?, forced?, channels: [{channel, on, points?}]}`:
  an oscilloscope's last acquisition: `seq` counts them, and when it is
  above `since` the screen's points come with it (volts, at the 8-bit
  codes, `dx_ps` apart from `x0_ps` across 12 divisions); `status` is the
  screen's WAIT, T'D, AUTO or STOP.
- `waveform.subscribe({probes: [...], max_rate_hz?}) -> {stream}`; samples arrive as `waveform.samples` notifications (`base64`) or binary frames; `waveform.unsubscribe({stream})`.
- Sample encodings: digital `u8` (`0,1,2=Z,3=X`), analog `f32`, serial bytes, value `f64`; each frame is monotonic in time; a `gap` marker reports dropped frames.
- Native JSON waveform frames carry `through_time_ps`, the producer's completed
  capture frontier. Intermediate bounded chunks stop at their last sample;
  the final chunk extends to the engine clock. Empty sample arrays may advance
  a constant signal's frontier. `restart: true` replaces the previous capture
  on subscription/reset. `gap: true` identifies evicted unsent history; normal
  chunk boundaries and eviction of already delivered samples are not gaps.

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
- `record.start({path?}) -> {from_ps}`; `record.stop({}) -> {path, hash, inputs, count, observations}` (with no path, `recording` is the document itself)
- `record.replay({path} | {recording}, allow_drift?) -> {ok, compared, divergence?: {index, expected, observed}, drift: [{what, recorded, now}]}`: the recording's project loaded into the session and replayed (SPEC-012 §2); `recording` is the document itself, as `record.stop` returns it without a path
- `record.to_scenario({path} | {recording}, assert?: ["probes", "serial"], name?) -> {scenario}`: the recording as a SPEC-007 scenario (SPEC-012 §3)

### analysis (the firmware's coverage, profile, stacks and faults)
- `analysis.start({board?, coverage?, profile?, stack?, faults?}) ->
  {coverage, profile, stack, faults}`: the boards' MCUs (one board's, or
  every board's with firmware) are watched from now on, counting from zero;
  coverage and the profile by default, the stacks and the faults when
  `stack` and `faults` are true. They stay on across
  a reset, a reload and a new image. `analysis.stop({})` turns them off.
- `analysis.coverage({board?, format?, detail?}) -> {boards: {ID: {lines:
  {hit, total}, functions: {hit, total}, insns, files: [{path, lines,
  functions: [{name, line, count}], counts: [[line, count]...]}],
  no_lines?}}}`; with `format` `text` or `lcov`, `{text}` (gcov's layout,
  an lcov tracefile). A line has code when the line table puts an address
  range of it inside a function; its count is its most-executed
  instruction's, a function's its first instruction's.
- `analysis.profile({board?, format?, detail?}) -> {boards: {ID: {insns,
  cycles, elapsed_cycles, functions: [{name, self_insns, self_cycles,
  incl_insns, incl_cycles, calls}]}}}` by self cycles (without `detail`,
  the first ten); with `format` `text`, `collapsed` or `collapsed-insns`,
  `{text}` (the report; collapsed stacks by cycles or instructions, a
  machine's cores rooted at `core0`, `core1`). `cycles` are the steps'
  (a sleeping core's time is no step's); `elapsed_cycles` the machine's.
- `analysis.stack({board?, format?, detail?}) -> {boards: {ID: {stacks:
  [{core, name, top, deepest, used, limit?, limit_from?, size?}],
  overflow?: {core, pc, function, sp, limit, limit_from}, functions:
  [{name, deepest, frame, incl}]}}}`: each core's stacks (`msp` and `psp`
  on a Cortex-M, else `stack`; addresses as the stack pointer holds them),
  the first overflow past the limit, the functions by depth (without
  `detail`, the first ten) with their own frame and their depth with their
  callees in bytes (SPEC-009 §12); with `format` `text`, `{text}`.
- `analysis.faults({board?, format?}) -> {boards: {ID: {count, faults:
  [{core, exception?, lockup?, name, pc, where, summary, handler?,
  backtrace: [{pc, where}], text}]}}}`: each fault the cores took, the
  first 16 (SPEC-009 §12), `text` the whole decoded report; with `format`
  `text`, `{text}`.
- Each is -32001 until `analysis.start` turns it on.

### toolchain
- `toolchain.detect({}) -> {profiles: [{name, title, found, version, program}]}` (`program`: the detect command's program as found, under the prefix the toolchain is installed with, SPEC-010 §1)
- `toolchain.build({board, profile?}) -> {ok, log, image}`

### scenario
- `scenario.run({path} | {scenario}, until?) -> {report}` (SPEC-007 §2; the session is reset first)
- `scenario.get({path}) -> {path, scenario}` and `scenario.save({path, scenario, add?}) -> {path}`:
  a scenario file of the project, `path` relative to its folder (no `..`, not
  absolute), written with two-space indents; `add` lists it in the project's
  `scenarios` (`project.save` keeps the list). The scenario editor's.

## 6. Notifications (server to client)

`diagnostics({records: [...]})`, `observe.update({now_ps, nets?, outputs?,
boards?})`, `waveform.samples({stream, seq, first_time_ps, encoding,
data})`, `control.state({running, now_ps, reason?})` (on run, pause, stop,
end), `debug.stopped({board, reason, pc, breakpoint?})`, `serial.data({target,
probe, base64})` (a serial probe's bytes since the last, after each observation
frame), `project.changed({tx, summary})`, `record.input({at_ps, seq,
target, input})`, `port.state({target, path, connected, baud, sketch_baud,
to_host, from_host})` (a host port's program opened or closed it, or set
another rate).

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
observed, expected, at_ps, measure?, unit?, window_ps?: {from, to}}], diagnostics: [...], statistics: {events,
instructions: {board: n}, analog_steps, wall_time_s}}`. With the analyses
on, it has `coverage` (each board's totals and files' counts, as
`analysis.coverage` without `detail`) and `profile` (each board's first ten
functions).

## 9. CLI exit codes

`0` success and all assertions passed; `1` an assertion failed; `2` usage or
load error; `3` a fatal diagnostic stopped the run; `4` replay divergence;
`5` toolchain build failed.

## 10. Conformance

`tests/protocol/` holds request and expected-response pairs and a Python
client (`tools/embsim_client`) that exercises every method against the
in-process and WebSocket transports; a method not covered there is not part
of the protocol.

### Schematic organization edits

`set_layout {groups: [{id, name, members}]}` replaces presentation groups.
Nonempty IDs/names, existing top-level members, unique group IDs and exclusive
membership are validated before replacement. `remove_component` and `remove_board`
prune memberships and empty groups. These rules also apply when loading projects.
`set_layout {id, layout: {x, y, rotation?, mirror?, locked?}}` supports placement
locks. Existing locked placement rejects position/orientation changes unless the
edit explicitly supplies `locked: false`; omission preserves a lock. Undo of a
lock must therefore explicitly unlock. Layout-only edits retain simulation time.


## Net observation: receivers and current contention

Net states include `contention` (boolean) and `receivers` (active digital input
terminal references, including board internals). A digital net retains its
resolved `logic`. An analog net with active digital receivers has `kind: "mixed"`,
retains `voltage` and adds aggregate sampled `logic`; different receiver levels
aggregate to X. Pure analog nets retain `kind: "analog"` without invented logic.

Contention means opposing known 0/1 drives at the strongest digital strength, or
opposing active digital output intents on a mixed island. Released drivers and
open-drain high outputs do not count. Analog supply-input bridges are excluded.
X alone is not proof of contention. This is current state, not a historical
fault latch, and does not classify all analog shorts or power conflicts.
