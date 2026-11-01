# 03 - Simulation Engine

**Status:** ACCEPTED. Contracts in SPEC-002 (time), SPEC-003 (nets and digital),
SPEC-004 (analog and bridges). Decisions: ADR-002, ADR-003, ADR-004, ADR-012.

---

## 1. Time

Virtual time is an unsigned 64-bit count of picoseconds (`es_time_t`). The
range is 2^64 ps, about 213 days of simulated time, enough for any experiment
the workbench runs and far beyond what a recording can hold. One picosecond
resolves every MCU clock in use (a 72 MHz core has a period of 13 888.9 ps)
and the propagation delays of fast logic (nanoseconds).

A **clock** is a rational period `num/den` picoseconds with a phase
accumulator: the k-th edge is at `floor(k * num / den)`. Rational clocks keep
a 72 MHz core and a 32.768 kHz RTC exactly phase-locked over any run length;
no floating-point time exists in the core. Derived clocks (PLLs, prescalers)
are rationals of their sources; a change of frequency at time `t` restarts the
accumulator from `t` so the edge sequence stays causal.

Time never moves backwards. `es_session_advance(until)` processes everything
up to and including `until` and leaves `now == until`. No subsystem reads the
wall clock; pacing (run at 1x, 0.1x, as fast as possible) is implemented in
the session by choosing `until` from the host clock in the client-facing `run`
loop, which changes nothing a model can observe.

## 2. The scheduler

One scheduler per session orders every state change. An event is
`(time, class, sequence, target, payload)`:

| Class (ascending priority at equal time) | Who schedules it |
|---|---|
| `EXTERNAL` | Session commands: inputs from users, scripts, replays. Applied first at their timestamp so firmware sees them "before" its own activity at that instant. |
| `CORE` | A core quantum boundary: the core has executed up to this time. |
| `PERIPHERAL` | Timer expiry, DMA completion, baud-rate edges inside an MCU. |
| `SIGNAL` | A pin or net change scheduled with a propagation delay. |
| `ANALOG` | An accepted analog step boundary or a reported threshold crossing. |
| `PROBE` | Sampling of a probe at its rate (never affects models). |

Within one `(time, class)` the sequence number (insertion order) decides.
This order is part of the contract (SPEC-002 §4); a change is an ADR.

**Delta cycles.** Zero-delay propagation (a functional gate, a wired net
resolution) runs inside the current timestamp: a change to a net notifies its
pins, which may schedule further zero-delay changes, processed in insertion
order until the timestamp is stable. A bound (`max_delta_cycles`, default
10 000) converts a combinational loop or an event storm into a diagnostic that
stops the run and names the nets involved.

**Core quanta.** A core backend executes instructions, converting its cycle
count to picoseconds through its clock, until the earlier of the next
scheduled event time and its quantum. A pin the firmware drives is scheduled
as a `SIGNAL` event at the core's current time, so a write to ODR at
12 345 678 ps changes the net at that picosecond, after everything earlier.
An `EXTERNAL` event at `t` is applied before the core executes the instruction
that would start at or after `t`; the core never runs past an input it should
have seen. The quantum (default 1 µs of core time) bounds the latency of a
pin change reaching another core or an analog island; a model that needs a
tighter bound asks the scheduler for a wake-up.

**Analog steps.** An analog island proposes its next step from its local
truncation error estimate; the scheduler clips the step to the next event
time; if a bridge input crosses a threshold inside the step, the step is
retried with a smaller size until the crossing time is bracketed within the
configured resolution (default 100 ps) and the crossing becomes an event.
Rejections are counted; a run that rejects every step is a diagnostic.

## 3. Nets and pins

A **net** holds the pins of the components it joins and a resolved state. Pin
models (SPEC-003 §2) are the vocabulary a component uses to say what its pin
is electrically; the engine decides from them how the net is solved:

| Pin model | Parameters | Digital island meaning | Analog island meaning |
|---|---|---|---|
| `digital_out` | drive (`push_pull`, `open_drain`, `open_source`, `tristate`), strength (`strong`, `weak`), Voh/Vol, Rout, slew | a driver with strength and value `0/1/Z` | a Thevenin source `Voh/Vol` through `Rout` with slew, or open |
| `digital_in` | Vih, Vil, hysteresis, pull (`none`, `up`, `down`) with Rpull, leakage | a weak driver from the pull; samples the resolved value | a comparator with hysteresis producing crossing events; the pull as a resistor to the rail |
| `analog` | none | forces the net to an analog island | a terminal of a device equation |
| `power` | nominal voltage, tolerance, role (`supply`, `ground`) | a `supply`-strength driver | an ideal source or a sink, unless a regulator component drives it |
| `passive` | none | a wire (resistance 0) | a terminal of a passive device |
| `nc` | none | unconnected, no diagnostic | unconnected, no diagnostic |

**Resolution in a digital island.** Values are `0`, `1`, `Z`, `X` with
strengths `supply > strong > weak > high_impedance`. The resolved value is the
value of the strongest drivers; two different values at the same strongest
strength give `X` and a contention diagnostic naming both drivers and their
components; no driver gives `Z` and, for inputs that sample it, a floating-input
diagnostic unless the input has a pull. Open-drain drivers never drive `1`.

**Islands.** A net with any `analog` pin, any `digital_out` whose component
declares `electrical` fidelity in an `electrical` session, or any passive
device that is not a plain wire, belongs to an analog island; nets joined by
passives merge into one island. Everything else is digital. The partition is
recomputed after a topology edit and reported to clients so the workbench can
show which wires carry voltages and which carry logic.

**Bridges** sit at pins whose model is digital but whose net is analog: the
engine inserts the Thevenin driver or the comparator from the pin model's
parameters. A session in `functional` policy may force all islands digital
(fast, for pure firmware work); `electrical` policy honours the models.

## 4. The digital engine

- Models are reactive: a component registers interest in its input pins and
  is evaluated when any of them changes, in delta cycles, or at scheduled
  times for its own clocks and delays.
- Propagation delay is a model parameter; `functional` models default to zero
  delay and `timed` models to their datasheet typical value, with min and max
  available for a timing-margin mode.
- Buses (UART, SPI, I²C, 1-Wire, CAN at the logic level) are modelled by
  components on nets, bit by bit, so a logic analyzer sees the real edges and
  a protocol decoder works on them. An MCU's UART is a peripheral model that
  drives its TX pin net bit by bit at the configured baud rate from its clock
  tree; a "virtual terminal" component on the same net decodes it.
- Diagnostics: contention, floating input sampled, delta-cycle bound, setup
  and hold violation where a model declares them, glitch shorter than a
  model's minimum pulse width.

## 5. The analog engine

Modified nodal analysis with Newton-Raphson iterations and adaptive-step
implicit integration (trapezoidal with BDF2 fallback, local truncation error
control), per island. Design points:

- **Operating point.** Gmin stepping then source stepping before giving up;
  failure is a diagnostic that names the nodes with the largest residuals.
- **Transient.** Step proposals from LTE; events clip steps; breakpoints from
  sources (PWL corners, pulse edges) are honoured exactly; threshold crossings
  at bridges are bisected to the configured resolution.
- **Linear algebra.** LU with partial pivoting in a minimum-degree order
  computed once per topology, eliminating over each pivot row's nonzeros
  (SPEC-004 §5); deterministic (the order is the topology's alone). Sparse
  storage for very large islands is not yet done.
- **Device models, 1.0:** resistor, capacitor, inductor, independent V and I
  sources (DC, pulse, sine, PWL, from a digital bridge), voltage-controlled
  switch, potentiometer, diode (Shockley with series resistance and optional
  breakdown), LED (diode plus luminous output as a function of current for the
  workbench), Zener, NPN and PNP BJT (Ebers-Moll in 1.0, Gummel-Poon when
  fixtures justify it), N and P MOSFET (level-1 Shichman-Hodges in 1.0, EKV
  later), ideal and behavioural op-amp, comparator, voltage regulator
  (behavioural), ADC sampling terminal and DAC/PWM source terminal.
- **Units and parameters.** SI with SPICE-style suffixes in descriptions;
  every parameter has a unit, a default and a range; out-of-range is a
  validation error.
- **Validation.** Golden fixtures generated offline by ngspice (committed with
  the generating netlist and version) and analytic cases (RC time constants,
  diode exponential, BJT bias points). Tolerances per fixture. ngspice is a
  development dependency only (ADR-004).

## 6. Mixed signal in practice

The acceptance circuit: PA0 configured as input with pull-up reads a push
button to ground; PC13 drives a 1 kΩ resistor into an NPN base; the collector
pulls an LED with a 220 Ω series resistor from 3.3 V. The button net is
digital (a `digital_in` pin with pull and a switch component); the base,
collector and LED nets form one analog island; PC13's pin model places a
Thevenin bridge. When firmware writes ODR, the bridge source steps with the
pin's slew, the island's transient advances, the LED current rises; the probe
on the collector shows the waveform; the workbench LED brightness follows the
LED model's luminous output. In `functional` policy the same project runs with
the LED as a logic indicator and no analog solve, which is what a firmware
developer wants on a slow laptop.

## 7. Determinism, reproducibility and performance

- The only nondeterministic inputs are external events; each is recorded with
  its timestamp and sequence (SPEC-012). Everything else is a function of the
  project, its images, the catalog and the engine version, whose hashes a
  recording carries.
- Floating-point in the analog engine is deterministic for a given platform
  and compiler flags; cross-platform bit-equality is not claimed, so fixtures
  carry tolerances and replay compares inputs and discrete observations, not
  raw voltages.
- Performance targets for 1.0 (benchmarks in CI, not promises): a Cortex-M3 at
  72 MHz with GPIO, timers and UART active runs at or above real time on a
  2020-class laptop in `functional` policy; an island of 30 nodes with two
  BJTs advances a 1 ms transient with 10 kHz PWM in under one second.
- Memory is bounded per session by configured limits (nets, components,
  events, probe buffers); exceeding a limit is a diagnostic, never an
  allocation failure deep in a model.
