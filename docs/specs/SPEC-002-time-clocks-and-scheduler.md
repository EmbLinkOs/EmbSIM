# SPEC-002 - Time, Clocks and the Scheduler

**Status:** Accepted 2026-10-15. **Builds on:** 03 §1, §2; ADR-002, ADR-016.
**Requirements:** `docs/requirements/TIME.md`.

---

## 1. Time

- `es_time_t` is `uint64_t` picoseconds since session reset. `ES_TIME_MAX` is
  `UINT64_MAX`; a session cannot advance to it (it is the horizon).
- Arithmetic on times is checked: an addition that would overflow is
  `ES_ETIME`; a subtraction of a later time from an earlier one is a bug.
- Conversion helpers: `es_time_from_ns`, `es_time_from_us`, `es_time_from_ms`,
  `es_time_from_s` (all exact), and the protocol representation: a decimal
  string of the picosecond count (`"12345678"`), or an object
  `{"ps":"12345678"}` where a type tag is needed. Clients never receive a
  JSON number for time.

## 2. Clocks

A clock is `(num, den, origin, count)`: period `num/den` ps as a reduced
rational with `den <= 2^20`, the time `origin` of edge 0, and the number of
edges elapsed. Edge `k` is at `origin + floor(k * num / den)`, computed in
128-bit-safe integer arithmetic. Operations:

- `esk_clock_set_rate(c, num, den, now)`: a new period from `now`; `origin`
  becomes `now`, `count` becomes 0; edges before `now` are unaffected.
- `esk_clock_next(c, after)`: the first edge strictly after `after`.
- `esk_clock_advance_cycles(c, n)`: returns the time of edge `count + n` and
  sets `count`; used by core backends to convert executed cycles to time.
- Derived clocks (`esk_clock_derive(parent, mul, div)`) are rationals of
  their parent; a parent rate change recomputes children from `now`.
- A disabled clock has `num = 0` and never produces edges; a model that
  depends on it does not advance.

Frequencies in descriptions are integers in hertz or rationals
`{"hz": 32768}` / `{"num": 1000000, "den": 72}`; the loader converts to
periods in picoseconds; a frequency whose period is not representable within
`den <= 2^20` is a validation error naming the value.

## 3. Events

An event is `{time, class, seq, target, payload}` where `class` is one of
`EXTERNAL, CORE, PERIPHERAL, SIGNAL, ANALOG, PROBE` in this ascending
priority order and `seq` is a 64-bit insertion counter. The queue is a binary
heap keyed by `(time, class, seq)`; equal keys cannot occur. Capacity is a
session configuration (`max_pending_events`, default 1 048 576); exceeding
it is `ES_ELIMIT` with diagnostic `sched.queue_full`.

Scheduling in the past (`time < now`) is `ES_ETIME`, except for the current
timestamp (`time == now`), which schedules a delta-cycle event of the given
class after every already-queued event at `now` of the same class.

Cancellation: events carry a generation number; cancelling an event marks
it; a marked event is skipped when popped. Models cancel their own pending
events on reset and on destruction.

## 4. Ordering contract

At each timestamp, the scheduler processes:

1. all `EXTERNAL` events in `seq` order;
2. then repeatedly, until no event remains at this timestamp: the lowest
   `(class, seq)` event; events it schedules at the same timestamp join the
   queue and are processed in their class and insertion order. A
   `delta_cycles` counter increments each time a `SIGNAL`-class event at the
   current timestamp is processed after another; exceeding
   `max_delta_cycles` (default 10 000) raises `sched.delta_overflow` (fatal)
   naming the last ten targets.
3. `PROBE` events run last at the timestamp, after all state has settled, so a
   probe never sees an intermediate value within a timestamp.

`now` then becomes the next event time not greater than `until`, or `until`.

## 5. Core quanta

A core backend is driven by the scheduler as follows. The session asks the
backend to execute until `min(next_event_time, now + quantum)` expressed as a
cycle budget through its clock. The backend returns having executed `n`
instructions and `c` cycles with its clock advanced; if it stopped early
(breakpoint, sleep, halt, fault), it says why. Pin writes performed during
execution are scheduled as `SIGNAL` events at the core clock's time of the
writing instruction's completion; the backend's bus calls `esk_sched_now_core`
for that time. The session then schedules the next `CORE` event at the
backend's clock time. If two cores interleave, each sees the other's pin
changes no later than its next quantum boundary or the next event, whichever
is earlier; the quantum is configurable (`core_quantum_ps`, default
1 000 000 000 = 1 µs) and may be reduced by a model requesting a wake-up at
an exact time (`esk_sched_wake_at`).

## 6. Analog steps

An analog island registers with the scheduler and is stepped by `ANALOG`
events. At each step the island proposes `h` from its error control; the
scheduler clips `h` so the step ends no later than the next queued event of
any class; the island integrates; if a bridge reports a crossing inside the
step, the island retries with the bisected `h` until the crossing is
bracketed within `crossing_resolution_ps` (default 100) and schedules the
crossing as a `SIGNAL` event; the step is then accepted and the next
`ANALOG` event scheduled at its end. A rejection counter per step beyond
`max_step_rejections` (default 50) raises `analog.step_failure` (fatal).

## 7. Determinism

Given the same project, images, catalog, engine version and the same sequence
of `EXTERNAL` events (time, seq, payload), the sequence of all other events
and all discrete observations is identical on every platform. The scheduler
contains no platform-dependent arithmetic. A conformance test replays a
recording through two sessions and compares the full event trace.

## 8. Pacing

`control.run` with a `speed` factor is implemented by the session choosing
successive `until` targets from the host clock: `until = now + speed *
host_elapsed`, in slices no larger than `run_slice_ps` (default 10 ms). A
speed of 0 means "as fast as possible". None of this is visible to models;
a recording of a paced run replays at any speed.
