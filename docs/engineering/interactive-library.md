# Interactive component library

This batch builds on main `5cc2762`. It adds 18 named entries and one reusable
relay contact controller: 147 components in total, including 44 named entries,
with 308 component fixtures. The three board fixtures bring engine catalog
validation to 150 items and 311 fixtures. This is an incremental electrical
library, not a claim of Proteus feature parity or hardware equivalence.

## Discover before placing

The library uses grouped categories with counts, multiword search across part
identity/function/package, manufacturer and package filters, model-tier filters,
and a named-parts filter. Custom categories remain in Other. Filters intersect;
Clear filters recovers an empty result. Entries sort naturally by name.

A keyboard-accessible preview opens without modifying the circuit. Overview,
Pins and Models show the original symbol, package identity, live controls,
numbered terminals, per-tier claims and omissions, and manufacturer link. A
source-review badge requires a hash for that exact manufacturer link; it does
not mean the model is electrically qualified. Place and drag are unavailable
for broken parts or parts with no model for the current project policy.

## Added parts

| Category | Exact entries | Model and interaction |
|---|---|---|
| Temperature | Vishay NTCLE100E3103JB0, NTCLE100E3472JB0, NTCLE100E3104JB0 | 10k, 4.7k, 100k NTC beta laws, live temperature, nominal divider fixtures at -20/25/85 C |
| Light resistance | Senba GL5528 | Live illuminance, illustrative power law, darkness/reference/bright fixtures |
| Photodiodes | Vishay BPW34, BPW34S | Live irradiance, nominal reverse photocurrent at 950 nm, simple forward knee |
| Optical isolation | Vishay VO617A-1, -2, -3, -4 | Four real CTR bins, LED knee, current transfer with finite collector saturation resistance; DC, linear CTR and switched-input fixtures |
| Trimmers | Bourns 3296W-1-103LF, 3296W-1-104LF | 10k/100k linear wiper; live position and two divider points |
| Switches | Omron D2F-01L, B3F-1000 | SPDT lever; four-pad tactile button with permanent 1/2 and 3/4 pairs, duplicate pads may remain open |
| Relays | Omron G5LE-1 DC5, G5LE-1 DC12 | R/L coil, separate contact circuit, hysteresis and persistent pickup/release timing |
| Connectors | Phoenix Contact MC 1,5/2-ST-3,81; MC 1,5/3-ST-3,81 | Independent numbered attachment terminals; adjacent pins never short together |

Every entry has original theme-aware schematic art, original illustrative package
art, numbered terminals, fidelity declarations and executable fixtures. Artwork
controls use hold/drag with keyboard equivalents. Numeric artwork text can apply
an optional 0..6 decimal `precision` after scaling; the simulation measurement is
unchanged. Connector artwork is not a footprint or mechanical model.

## Sources and model boundaries

Manufacturer PDFs were retrieved, read and hashed outside the checkout. Reviewed
entries store exact revision, SHA-256 and pages in `provenance.sources`. No vendor
PDFs or artwork are redistributed.

| Document | Reviewed scope |
|---|---|
| Vishay 29049, 7 May 2025, pp. 1–2 | NTC ordering codes, R25 and B25/85. Nonpolar lead numbers are an ECAD convention. |
| Vishay 81521, rev 2.1, 23 August 2011, pp. 1–2, 4 | BPW34/S package/cathode mark and nominal 50 uA at 10 W/m2, 950 nm. K1/A2 are ECAD labels, not manufacturer-numbered leads. |
| Vishay 83430, rev 2.8, 22 January 2025, pp. 1–3 | VO617A DIP-4 A1/K2/E3/C4 and CTR bins. Selected nominal CTR values are illustrative, not statistical/device fits. |
| Omron K100-E1-08, March 2018, pp. 1–4 | G5LE-1 ordering, SPDT contact form, package and 10/5 ms time limits; KiCad Relay.lib at 4e17d759 supplements coil 2/5, COM1/NC4/NO3. |
| Omron B036-E1-12, March 2018, pp. 2, 4, 6 | D2F-01L model and COM/NO/NC functions. Simulator numbers COM1/NC2/NO3 are an ECAD convention; the manufacturer drawing identifies functions. |
| Omron A070-E1-08, October 2014, pp. 1, 3–4 | B3F-1000 ordering and top-view internal pad connections. Actual leads are unmarked. |

There are now 38 source-reviewed named entries, including the 25 from the first
batch. Six remain reference-only: BAT54, GL5528, the two Bourns trimmers and the
two Phoenix plugs. Bourns requests returned HTTP 403; Senba/Phoenix were blocked
by the cloud proxy. Their manufacturer links and unreviewed status are retained;
no revision/hash is fabricated. GL5528's 15k at 10 lux / gamma 0.7 / 1 Mohm dark
curve is an educational approximation. Bourns mechanical orientation and Phoenix
mechanical drawings still need manufacturer review.

The NTC equation is `R25 * exp(B * (1/T - 1/298.15))`, T in kelvin. It is not
Vishay's full polynomial curve. Neither NTC nor LDR models self-heating, thermal
inertia, light adaptation or spectral response. Photodiodes use a fixed reference
wavelength, without capacitance, speed, temperature dependence or reverse breakdown.

Optocouplers use a piecewise LED knee, fixed CTR, and collector current bounded
by `VCE / r_sat`. This keeps abrupt switches solvable while retaining an unsaturated
CTR region. The default 30 ohm saturation resistance is illustrative. No delay,
CTR temperature/current curve, reverse collector operation or dielectric behavior
is qualified. The offset-ground fixture checks that the model creates no galvanic
stamp between input and output; it does not certify isolation.

Relays reuse the existing resistor/inductor engine for the coil and a separate
contact controller. Coil resistance is nominal; 100 mH is an illustrative,
unmeasured inductance. Datasheet maximum operate/release times are used as fixed
model delays, not simulated mechanical distributions. A 100 us sampling bound
checks pickup/dropout and delayed state changes affect the next solve (up to
200 us timing error). A short pulse cannot complete pickup. There is no contact
bounce, arc, magnetic saturation, wear or temperature rise. A freewheel diode
changes RL decay and can therefore delay dropout; it is not hidden in the relay.

## Four executable circuits

- [Temperature divider](../../examples/temperature-divider/README.md): NTC and
  adjustable trimmer feed LM393P's LED alarm.
- [Light-controlled switch](../../examples/light-controlled-switch/README.md):
  GL5528 drives a 2N3904 and LED; Phoenix terminals expose the supply.
- [Optocoupled input](../../examples/optocoupled-input/README.md): lever switch
  drives VO617A-3; the input ground is offset by 2 V from the output reference.
- [Transistor relay](../../examples/transistor-relay/README.md): 2N3904, R/L
  relay coil, 1N4007 flyback diode and independent 12 V contact load.

All support live controls in Run and a CLI scenario against the shared engine.
Their assertions include solver health. LED-loaded outputs are checked against
their actual load, not an ideal unloaded rail.

## Regression workflow

```sh
python3 tools/validate.py --all
python3 tools/catalog/run_fixtures.py --embsim build/dev/bin/embsim
python3 tools/ci/run_examples.py --build build/dev
python3 tests/browser/test_library.py --embsim build/dev/bin/embsim --chromium /usr/bin/chromium
```

The browser regression needs optional Python Playwright and an installed Chromium
(or its Playwright browser when `--chromium` is omitted). It launches the real
engine and checks filters, previews, placement, both themes, controls and waveform
values during press/release. It also verifies that a circuit edit reloads runtime
inputs and resubscribes waveforms. Layout-only edits retain the current run.
Screenshots can be saved with `--screenshots DIR`; no source project is overwritten.

The earlier real-engine workflow could show empty traces after placing a part:
the engine rebuild discarded subscriptions while the client retained their IDs.
Transactions reporting `restarted: true` now reload session bindings and clear
obsolete waveform records before subscribing again. This is covered by the
placement-then-run regression, which checks actual received samples, not a mock.
