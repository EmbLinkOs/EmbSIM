# SPEC-004 - Analog Engine and Mixed-Signal Bridges

**Status:** Accepted 2026-10-15. **Builds on:** 03 §5, §6; SPEC-002 §6;
SPEC-003 §4; ADR-004. **Requirements:** `docs/requirements/ANA.md`.

---

## 1. Scope

The per-island analog solver, its device model interface, numerical methods,
diagnostics, validation fixtures and the bridges to digital islands.

## 2. Formulation

Modified nodal analysis. Unknowns are node voltages (ground is node 0, fixed
at 0 V) and the branch currents of voltage sources, inductors and other
current-defined elements. Each device contributes stamps to the Jacobian `J`
and residual `f` for the current operating point; the engine solves
`J Δx = -f` and iterates (Newton-Raphson) until `|Δx| < reltol·|x| + abstol`
for every unknown and the residual norm is under `iabstol`, or `max_iter`
(default 100) is reached.

Defaults (per session, overridable per island in the project): `reltol
1e-3`, `vntol 1e-6` V, `abstol 1e-12` A, `gmin 1e-12` S, `max_iter 100`,
`h_min 1 ps`, `h_max` from the island's fastest source or 1 ms, `lte_tol
1e-3`, `method trapezoidal` with BDF2 fallback after two rejections.

## 3. Operating point

1. Try direct Newton from the initial guess (zeros, or the last accepted
   solution after a topology transaction).
2. On failure, gmin stepping: `gmin` from 1e-2 down to the configured value in
   decade steps, each converged from the previous.
3. On failure, source stepping: all independent sources scaled from 0 to 1 in
   steps halved on failure.
4. On failure, `analog.no_convergence` (fatal in `strict`, error otherwise:
   the island freezes at its last state and reports) naming the three nodes
   with the largest residuals and the devices attached.

## 4. Transient

- Integration state per reactive device (charge or flux history for
  trapezoidal and BDF2); the engine owns the step proposal from the local
  truncation error estimate (`h_new = h · (lte_tol / lte)^(1/3)`, clamped to
  `[h/4, 2h]`).
- Breakpoints: every source reports its next discontinuity (pulse edges, PWL
  corners, a bridge's scheduled transition); the step never crosses one.
- Scheduler coupling is SPEC-002 §6: proposals are clipped to the next event;
  bridge crossings cause retries with bisection to `crossing_resolution_ps`.
- A step is accepted when Newton converges and the LTE is under tolerance;
  otherwise `h` is halved and retried; `max_step_rejections` consecutive
  rejections raise `analog.step_failure`.
- Timestep too small (`h < h_min`) raises `analog.timestep_too_small` naming
  the device with the largest LTE contribution.
- A steady island stops solving. After two accepted trapezoidal steps in a
  row that change no unknown (by more than 1e-14 relative), neither ending
  at a breakpoint, with no breakpoint pending, and when a discarded trial
  step of `h_max` from that solution changes nothing either, the island is
  quiet: it is not stepped at all until a touch (a bridge's new drive, an
  input) wakes it at the touch's time, or until its next breakpoint; its
  time catches up when it wakes. A busy MCU's quanta clip every step
  to a microsecond, and a board's LED and pull-up islands then cost nothing
  between edges; an island still moving, however slowly, is solved to the
  solver's own floor (the second step solves what the first's `accept`
  changed: a switch settling, a ramp ending).

## 5. Linear solver

Dense LU with partial pivoting for islands up to 64 unknowns; a sparse
direct solver (Markowitz ordering computed once per topology, numeric
refactorization per Newton step) above that. Pivot order is a function of
the topology only, so results are deterministic for a given platform and
flags. Singular matrices (a floating node, two ideal sources in parallel)
are detected at topology time and reported as `analog.singular_topology`
naming the node; `gmin` to ground is added to every node so a floating node
yields 0 V with a warning rather than a failure.

## 6. Device model interface

```c
typedef struct esk_adev_ops {
    const char *name;
    int  (*setup)(esk_adev *d, esk_island *isl);          /* allocate nodes/branches, validate params */
    void (*load_dc)(esk_adev *d, esk_amatrix *m);          /* stamps for the operating point */
    void (*load_tr)(esk_adev *d, esk_amatrix *m, const esk_integ *in); /* stamps with integration state */
    void (*accept)(esk_adev *d, es_time_t t);              /* a step was accepted: commit history */
    int  (*next_breakpoint)(esk_adev *d, es_time_t after, es_time_t *at);
    double (*lte)(esk_adev *d, const esk_integ *in);       /* local truncation error contribution */
    void (*observe)(esk_adev *d, esk_obs *o);              /* currents, powers, derived outputs (LED luminance) */
} esk_adev_ops;
```

Devices are registered in the model registry as primitives and bound from
component descriptions by name. Parameters are typed, with units and
ranges, from the description (SPEC-005 §4).

## 7. Device models, version 1

| Primitive | Parameters (SI, defaults) | Equations and notes |
|---|---|---|
| `resistor` | `r` (Ω, required), `tc1`, `tc2` (0), `temp` (27 °C) | linear; `r` must be > 0 |
| `capacitor` | `c` (F, required), `ic` (V, none), `esr` (0) | trapezoidal/BDF2 companion |
| `inductor` | `l` (H, required), `ic` (A, none), `dcr` (0) | branch current unknown |
| `vsource` | `kind`: `dc`, `pulse`, `sine`, `pwl`, `exp`; standard SPICE parameters | reports breakpoints; `dc` default |
| `isource` | same kinds | |
| `vcvs`, `vccs`, `cccs`, `ccvs` | `gain` | linear dependent sources |
| `switch` | `ron` (1 mΩ), `roff` (1 GΩ), `state` (bool, an interactive input), `bounce` (optional: `{count, interval}`, or `true` with `bounce_ms`: six transitions spread evenly over that time) | resistance switched by the input; bounce generates breakpoints |
| `potentiometer` | `r` (Ω), `position` (0..1, interactive), `taper` (`linear`, `log`) | two resistors |
| `diode` | `is` (1e-14 A), `n` (1), `rs` (0), `bv` (none), `ibv`, `cj0` (0), `vj`, `m`, `tt` (0) | Shockley with series `rs`, optional breakdown, junction and diffusion capacitance; limiting on the exponential (pnjlim) |
| `led` | diode parameters with defaults per `color` (`red`, `green`, `blue`, `yellow`, `white`): `vf` typical and `is`/`n` fitted; `if_nom` (20 mA); `efficiency` curve | `observe` reports current and a normalised luminance (0..1 at `if_nom`, saturating) for the workbench |
| `zener` | diode plus `vz`, `rz` | breakdown model |
| `bjt_npn`, `bjt_pnp` | `is`, `bf` (100), `br` (1), `nf`, `nr`, `vaf` (none), `rb`, `rc`, `re` (0), `cje`, `cjc`, `tf`, `tr` (0) | Ebers-Moll with Early effect and optional charges; Gummel-Poon parameters (`ikf`, `ise`, `ne`) accepted and used when present |
| `nmos`, `pmos` | level-1: `vto`, `kp`, `lambda`, `gamma`, `phi`, `w`, `l`, `cgs`, `cgd`, `cgb`; `rds` (none) | Shichman-Hodges with cutoff, linear, saturation; body tied to source unless a `B` pin is connected |
| `opamp` | `gain` (1e5), `vsat` from supply pins or `vpos`/`vneg`, `gbw` (none), `slew` (V/µs, none), `rout` (0), `rin` (1e12) | behavioural with saturation; single-pole when `gbw` is given, slew-limited when `slew` is |
| `comparator` | `vhyst` (0), `tpd` (0), output pin model from description | outputs through a digital_out pin model (a bridge) |
| `regulator` | `vout`, `dropout`, `imax`, `quiescent` | behavioural LDO: `vout` while `vin > vout + dropout`, else tracks; current limit |
| `adc_terminal` | `rin`, `cin`, `sample_time` | an MCU's analog input; `observe` samples the node for the peripheral model |
| `dac_source` | `vref`, `bits`, `rout`, `settling` | an MCU's analog output or PWM filter source |

A model's `electrical` fidelity declaration lists which effects are
implemented; a parameter outside the declared range raises
`analog.param_out_of_range` at load.

### Implemented catalog primitives added with the named-part batch

- `comparator_oc`: pins `+`, `-`, `OUT`, `V+`, `V-`; a smooth open-collector
  conductance with `ron`, `roff`, `transition` and `min_supply`. External pull-up
  required; no delay, common-mode enforcement or offset model.
- `temperature_sensor`: pins `OUT`, `VS`, `GND`; externally set `temperature`
  in °C, `offset + slope * temperature`, finite `rout`, supply clipping using
  `headroom`. Output observation is relative to the device's `GND`, which need
  not be global zero. LM35DZ declares a 4..30 V supply envelope and TMP36GT9Z
  2.7..5.5 V, with endpoint fixtures for nominal transfer only. No thermal
  integration, supply-dependent accuracy, out-of-range behavior or noise model.
- `timer555`: pins `TRIG`, `THRESH`, `RESET`, `CTRL`, `OUT`, `DISCH`, `VCC`,
  `GND`; 5 kΩ + 10 kΩ divider, trigger at half CTRL, threshold at CTRL,
  reset priority, accepted-step latch and discharge. `sample_interval` bounds
  the threshold sampling interval (1 us default). Switching affects the next
  solve, giving up to twice that interval of timing error. No vendor timing or
  transistor-level accuracy is claimed.
- `opamp` additionally accepts nonnegative `headroom_high` and `headroom_low`
  (0 by default), moving its DC output limits inwards from the supply rails.
  The named LM358P and LMV358 use them.
- `opamp` with `gbw` has its dominant pole. An internal node y (a second
  branch unknown) integrates the input stage's drive, as the compensation
  capacitor does: `dy/dt = SR tanh(wu vd / SR) g - (y - vmid) / tau`, with
  `wu = 2 pi gbw`, `tau = gain / wu` and `SR = slew` (without `slew`, the
  drive is `wu vd`). So:
  - **Small signals.** DC gain `gain`, unity-gain frequency `gbw`.
  - **Large steps.** The output slews at `slew`.
  - **Saturation.** `g`, a sigmoid 10 mV wide, stops the drive pushing y
    past the output limit it is already at, so there is no windup: the
    amplifier comes out of saturation within tens of nanoseconds.
  - **Output.** y clamped at the limits with 10 mV corners, through `rout`.
    The output and y depend on the supply pins, so a loop through the
    amplifier's own supply converges.
  - **Operating point.** The static characteristic (`vmid + vamp tanh(gain
    vd / vamp)`) gives the output and y; it equals the pole's equilibrium in
    the linear range.
  - **Newton limiting.** In both forms, Newton moves tanh's argument at most
    4 per iteration, linearised from the limited point, as `pnjlim` does for
    a junction. Deep in saturation tanh's slope underflows, and a follower
    whose input hangs on its own output would otherwise bounce between the
    rails.
  - **Not modelled.** Further poles, phase margin, input offset and bias,
    the input common-mode range and output current limits.

These implemented behavioral primitives do not imply that every target model
in the table above exists. Pin order is binding for native analog devices.
Fixtures exercise DC, temperature changes, comparator loading, 555 latch/reset,
and an RC astable against the nominal `1.44 / ((RA + 2 RB) C)` frequency.

### Interactive electrical catalog devices

`environment_resistor` has pins `1`, `2` and an external temperature (NTC beta
law) or illuminance (LDR power law) input. `photodiode` has `A`, `K`, a reference
irradiance input, reverse photocurrent and a simple forward knee. `optocoupler`
has `A`, `K`, `C`, `E`, an LED knee and nominal CTR capped by collector saturation
resistance; no galvanic input-output stamp is introduced. These are low-frequency
approximations with per-entry limits, not full device or optical models.

`ptc_fuse` (pins `1`, `2`) is a polymeric PTC resettable fuse, a resistance that
heats itself:
- **Thermal model.** Two thermal nodes: the polymer core (`c_core`, `r_core`
  to the package) and the package (`c_case`, `r_case` to the room). The room
  is the input `temperature`, bound to the world's.
- **Resistance.** `r0` at 23 C, rising by `slope` per kelvin below
  `t_switch`, then a factor e every `trip_k` kelvin above it, so the heat runs
  away above a current and the fuse trips.
- **Its own clock.** The temperatures are integrated apart from the island's
  steps. Each accepted solution sets the voltage held until the next. Looks
  come about half a kelvin of change apart, a second at most once settled,
  each a component wake (`on_time`) dispatched to the device. A look already
  due by then is kept. Moving it on every accepted step would leave one
  cancelled event per step in the scheduler, which every step scans.
- **Re-solving.** The matrix holds the resistance last stamped; a 1 % change
  touches the island, which then solves again. A quiet circuit therefore
  sleeps between looks while the fuse warms over minutes.

`relay_contacts` senses `S+`/`S-` across the separate coil resistance and switches
`COM` between `NC` and `NO` after persistent pickup/dropout thresholds and delays.
Only accepted solver times commit state; rejected trials cannot advance a switch.
Its monitoring interval is 100 us; switching affects the following solve, for up
to 200 us timing error. Package composites reuse the existing R/L primitives for
coil dynamics. `spdt_contact` exposes `COM`, `NC`, `NO` controlled by `pressed`.
`pushbutton4` preserves the permanent pad pairs 1/2 and 3/4 and accepts unused
duplicate pads left open; `pressed` bridges the pairs through finite resistance.
These two manual contacts omit bounce. See [the interactive library record](../engineering/interactive-library.md)
for equations, sources, default assumptions and executable examples.

## 8. Bridges (SPEC-003 §4 restated numerically)

- **Thevenin driver**: for value `1`, a source `voh` in series with `rout`;
  for `0`, `vol` through `rout`; for `Z`, open. A transition ramps linearly
  at the pin's slew rate (V/µs) from the current source value; the ramp's
  start and end are breakpoints. `rout` depends on the configured drive
  strength when the MCU description gives a table.
- **Comparator**: the node voltage versus `vih`/`vil` with hysteresis;
  crossings are detected by sign change of `v - threshold` between accepted
  steps and bisected to `crossing_resolution_ps`; the digital value changes
  at the bracketed time as a `SIGNAL` event with the pin's `tpd`.
- **Model comparator**: a model may set a second threshold on any of its
  pins on an analog net (`esk_pin_compare`, `esk_pin_compare_off`), as an
  MCU's analog comparator does at the other input's voltage. Its crossings
  are found and bisected like the comparator's and call the model's
  `on_input` at the bracketed time; the pin's own thresholds and digital
  value are untouched.
- **Pull**: `rpull` to the supply or ground node of the island; the supply
  node is the net of the pin's `power` reference in the component description
  (`vdd_pin`), else an implicit ideal source at the family's nominal voltage,
  reported as `analog.implicit_supply` (info).
- **ADC terminal** and **DAC source** as in §7.

## 9. Observations

Per island per accepted step: node voltages; per device: currents, power
and model-specific outputs (luminance, operating region, Vce, Ic). Probes
subscribe to any of these (`probe.add` with target `net:`, `pin:`,
`device:U3.ic`). Samples are emitted at accepted steps (variable rate) and
optionally resampled at a fixed probe rate for instruments.

## 10. Validation fixtures

`tests/analog/fixtures/<name>/` holds `circuit.json` (an EmbSIM project or
a bare island), `expected.json` (operating point or sampled waveform with
tolerances), and `source.md` naming the generator: an ngspice netlist and
version (`tools/analog/ngspice_fixture.py` regenerates), or an analytic
derivation. Required at M0: RC charge and discharge, RL, voltage divider,
diode forward curve at five currents, LED with series resistor at 3.3 V and
5 V, NPN common-emitter switch (cutoff and saturation), NPN emitter follower,
NMOS switch, PWM into an RC filter (ripple and mean), op-amp inverting
amplifier, comparator with hysteresis, potentiometer divider, bouncing switch
into a pull-up (crossing events count).

## 11. Diagnostics

`analog.no_convergence`, `analog.step_failure`, `analog.timestep_too_small`,
`analog.singular_topology`, `analog.param_out_of_range`,
`analog.implicit_supply`, `analog.overvoltage` (a device's declared absolute
maximum exceeded, warning), `analog.overcurrent`.
