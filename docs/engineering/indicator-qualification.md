# Named indicator evidence

This first indicator batch adds Kingbright WP7113ID and WP154A4SUREQBFZGC.
It checks **nominal DC terminal curves at 25 °C, 2–20 mA**, terminal identity and
body envelopes. It does not certify complete devices or reproduce manufacturer
process corners, optical output, thermal response, damage or transient behavior.
A manufacturer's part number and a source hash alone do not qualify a model.

## Sources and supported envelope

| Part | Reviewed source | Checked package/terminals | DC curve tolerance |
|---|---|---|---|
| WP7113ID | [Kingbright DSAF0012 V.14A, 2026-01-08](https://www.kingbrightusa.com/images/catalog/SPEC/WP7113ID.pdf), pp.1–3 | 5 mm lens, 5.9 mm flange, 8.6 mm body, 2.54 mm leads; long anode and marked short cathode | ±0.035 V against source curve samples |
| WP154A4SUREQBFZGC | [Kingbright DSAJ8687 V.11A, 2025-03-25](https://www.kingbrightusa.com/images/catalog/SPEC/WP154A4SUREQBFZGC.pdf), pp.1–3 | 5/5.9/8.6 mm body; 1.27 mm leads, **1 R, 2 COM cathode, 3 B, 4 G** | ±0.060 V per channel against source curve samples |

WP7113ID's source identifies polarity without numeric terminals; catalog numbers
1=A and 2=K are explicitly conventions. The RGB numbers are manufacturer numbers.
The RGB part cannot be substituted geometrically for the generic R/COM/G/B entry.
Four 1.27 mm leads do not fit four consecutive 2.54 mm breadboard holes. Lens
curvature, formed lead lengths, contact mechanics and the rendered 3 mm clearance
are representative. Package outline/pitch checks do not establish manufacturing
or connector-mating qualification.

Each component embeds `qualification` with the exact PDF SHA-256 and revision,
page-3 plot axes/path indices, independently extracted current/voltage samples,
calibration/holdout roles, current envelope, temperature and acceptance tolerance.
Four calibration currents fit each channel. Six other currents are holdouts and
never determine coefficients. These are typical source curves, not measurements
of an individual production device or guaranteed corner models. The tabulated
1.9 V at 10 mA for WP7113ID and 1.9/3.3/3.3 V at 20 mA for RGB R/B/G provide
separate source sanity anchors. Their maximum limits and manufacturer variation
must not be inferred from these typical fits.

## Electrical implementation

At the fixed reference, terminal voltage is fitted as
`V = a + b ln(I/Iref) + rs (I-Iref)`. The native Shockley junction receives
`n=b/VT`, `vf=a-Iref*rs`, and `if_nom=Iref`. In particular, `vf` is a **junction**
drop; a terminal-voltage datum also includes `I*rs`. The engine's fixed VT constant
is retained as an effective static-fit reference; it does not implement a
25-to-27 °C temperature transfer or any temperature sweep.

RGB channels have independent ideality, series resistance and reference current.
Only positive series resistances allocate internal solver rows; mixed zero and
nonzero resistance cannot leave unused matrix rows. Optional channel `*_rs` and
`*_if_nom` parameters inherit the generic part's global values when absent.
`required: false` allows that absence without weakening validation of a supplied
value. Existing parameters without defaults remain required by default. Named
parts bind immutable curve constants; users can author separate custom models.

Unconnected LED/RGB terminals retain their named slots and receive private
floating analog nodes. They do not silently become ground or compress neighboring
pins into another terminal's role. No fabricated connection is added to project
nets. This correction is scoped to indicator models; it does not claim equivalent
qualification of all analog primitives' optional-terminal conventions.

## Reproducible checks

```sh
python tools/catalog/indicator_evidence.py
python tools/catalog/indicator_evidence.py --pdf /path/WP7113ID.pdf --pdf /path/WP154A4SUREQBFZGC.pdf
python tools/catalog/run_fixtures.py --embsim build/ui-layout-native/bin/embsim --component wp7113id --component wp154a4sureqbfzgc
embsim run examples/qualified-rgb-driver/project.json --scenario examples/qualified-rgb-driver/colors.json --json
```

The optional PDF reproduction command needs PyMuPDF. No PDF download or optional
Python dependency is required in ordinary CI, and vendor PDFs are not redistributed.
The command verifies a supplied source snapshot's hash and reproduces its vector
plot samples. The ordinary validator checks source/provenance agreement,
calibration-only coefficients, disjoint holdouts, declared envelopes and native
fixture identities/topology/bounds. Native fixtures execute separately through the
same CLI as user circuits; file presence or a computed fit is not execution proof.

Current-source fixtures force each source current and check native terminal
voltage against source-derived bounds. Loaded circuits independently interpolate
the source points to check 3.3 V and 5 V load lines, finite 25 Ω representative
drivers, external limiting resistors and source-current conservation. Other
fixtures cover off, reversed supply, floating common with unequal neighboring
voltages, all ideal RGB combinations, and mixed/zero internal resistance. These
are not qualification of a particular MCU's GPIO driver or perceived PWM light.

The [driver example](../../examples/qualified-rgb-driver/README.md) uses the real
named model, physical switch inputs and the exact pin map. Its all-mix CLI scenario
and the workbench consume the same solver. The library Models tab and selected
part inspector show this bounded evidence and its source, separately from ordinary
functional/timed/electrical availability badges. Brightness/color remain normalized
illustrative display outputs; no lumen, candela or calibrated-white claim is made.

## Follow-on qualification

Manufacturer corners, low-current/reverse characterization, junction charge,
temperature/thermal coupling and firmware/PWM acceptance remain additional gates.
The [construction plan](circuit-construction.md) prioritizes named logic/shift
registers, analog/driver, supply and sensor families next. Connected instruments
then require demonstrated input resistance/capacitance and ammeter burden, with
source-backed ranges and accuracy. Ideal inspection probes remain separate.
