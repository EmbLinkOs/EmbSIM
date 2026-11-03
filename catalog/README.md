# Component library

The library contains generic building blocks and searchable manufacturer part
entries. Categories describe function; the optional `part` object describes the
manufacturer, exact part number and selected package. Pin numbers are tied to
that package, not inferred from a generic transistor or gate symbol.

**Named does not mean manufacturer-qualified.** These are original behavioral
or parameterized approximations, not vendor SPICE macromodels. Every entry lists
its omissions. Electrical fixtures check stated nominal behavior; they do not
certify datasheet limits, temperature corners, destructive limits, RF behavior,
or suitability for safety-critical designs.

## Part families

Most real parts come in families: one datasheet, one table, many part
numbers. `catalog/families/<family>/family.json` holds such a table as the
datasheet gives it, one row per part number, beside a component template and
fixtures written with `${var}` placeholders; `tools/catalog/families.py
generate` expands it into ordinary entries under `components/` (French
text included) and `check` (ctest `catalog_families`) fails when an entry
differs from its family. Edit the family, never the generated entry.

Each row's values are read from the datasheet, and `families.py verify`
proves it: it downloads each datasheet, checks the sha256 the family
records and finds every row's `evidence` (its table line, its column) in the
PDF's text. The fixtures then hold the model to the same table's limits.

| Family | Parts | Datasheet | Fixtures |
|---|---|---|---|
| `rectifiers_1n400x` | Vishay 1N4001 to 1N4007, 1 A, DO-41 | 88503 | VF at 1.0 A under 1.1 V; IR at VRRM under 5 uA |
| `rectifiers_1n540x` | Vishay 1N5400 to 1N5408, 3 A, DO-201AD | 88516 | VF at 3.0 A under 1.2 V; IR at VRRM under 5 uA |
| `schottky_1n581x` | Vishay 1N5817, 1N5818, 1N5819 | 88525 | VF at 1.0 A and 3.1 A under each part's maxima; IR at VRRM under 1 mA |
| `zeners_bzx55` | Vishay BZX55C2V4 to C75 and B2V4 to B75 (74), 500 mW, DO-35 | 85604 | VZ at IZT within the table's minimum and maximum; IR at VR; VF at 200 mA |
| `zeners_1n47xx` | Vishay 1N4728A to 1N4761A (34), 1.3 W, DO-41 | 85816 | VZ at IZT within 5 %; IR at VR; VF at 200 mA |
| `zeners_1n52xx` | Vishay 1N5221 to 1N5267 in their B (5 %) and C (2 %) tolerances (94), DO-35 | 85588 | VZ at IZT within the suffix's tolerance; IR at VR; VF at 200 mA |
| `zeners_bzx79` | onsemi BZX79C2V4 to BZX79C18 (21), DO-35 | BZX79C2V4/D | VZ at IZT within the table's minimum and maximum; IR at VR; VF at 200 mA |
| `bridges_w0xg` | Vishay W005G to W10G 1.5 A bridge rectifiers, WOG (7): four junctions in a composite | 88769 | full-wave rectification both ways into 10 ohm under the per-diode VF maximum |
| `switching_1n914` | onsemi 1N914, 1N914A, 1N914B, 1N916, 1N916A, 1N916B, 1N4148, 1N4448, DO-35 | 1N914/D | each grade's VF window or maximum at its test currents; IR at 20 V and 75 V; breakdown at the 100 V minimum |
| `bjt_bc546` | onsemi BC546B, BC547A, BC547B, BC547C, BC548B, BC548C (NPN), TO-92 | BC546/D | IC within the hFE group's limits at 2 mA and VBE(on) inside 0.55 to 0.7 V; VCE(sat) at 10 mA; ICES |
| `bjt_bc556` | onsemi BC556B, BC557A, BC557B, BC557C, BC558B (PNP), TO-92 | BC556B/D | the same, with the PNP's limits |
| `leds_kingbright` | Kingbright 5 mm WP7113 and 3 mm WP710A10 LEDs in their colours and lenses (23), one datasheet each | each part's | VF at the datasheet's test current under its maximum; IR at 5 V |
| `transformers_triad_f`, `transformers_triad_f_ct` | Triad Magnetics F series 115 V chassis-mount power transformers, plain and centre-tapped (13) | each part's | the secondary's peak at full load (sqrt 2 of the rated voltage, within 5 %) |
| `optocouplers_sfh617a`, `optocouplers_cny17`, `optocouplers_4n35` | Vishay SFH617A-1 to -4 (DIP-4), CNY17-1 to -4 and 4N35 to 4N37 (DIP-6, base brought out) (11) | 83740, 83606, 81181 | the CTR within its rank at 10 mA; VF; VCE(sat) |
| `opamps_ti_lm358`, `opamps_ti_tl072`, `opamps_ti_ne5532` | Texas Instruments LM358, LM258 and LM2904 (classic and B versions), TL072 (C, AC, BC, I and H) and NE5532 and SA5532 dual op-amps, every orderable package (40) | SNOSBT3, SLOS080W, SLOS075K | a follower; the output swing from the rails; the slew rate; the gain-bandwidth's single pole |
| `mosfets_vishay_irf_n`, `mosfets_vishay_irf_p` | Vishay Siliconix IRF510 to IRF840, IRFZ14 to IRFZ48, IRL510 to IRL540 (N, 33) and IRF9510 to IRF9640, IRF9Z14 to IRF9Z34 (P, 10), TO-220AB: a level-1 channel, drain resistance, body diode and capacitances in a composite | each part's | off; RDS(on) at its maximum; VGS(th) at 250 uA; the body diode; the input capacitance's time constant |
| `opamps_ti_lm324` | Texas Instruments LM324, LM324A, LM324K, LM224, LM224A, LM2902, LM2902KV, LM2902KAV and the LM324B and LM2902B quad op-amps, every orderable package (23) | SLOS066AE | a follower; the positive swing; the slew rate; the bandwidth, on channels 1 to 4 |
| `inductors_bourns_rlb` | Bourns RLB0608, RLB0712, RLB0812, RLB0912, RLB0914, RLB1014 and RLB1314 radial lead inductors, 1 uH to 100 mH (203): the inductance, its maximum DC resistance and a parallel capacitance at the minimum SRF | RLB | the drop at the rated current; the L/R time constant |
| `bjt_onsemi_tip31`, `bjt_onsemi_tip32`, `bjt_onsemi_bd135`, `bjt_onsemi_bd136`, `bjt_onsemi_2n3055`, `bjt_onsemi_mj2955` | onsemi TIP31A/B/C and TIP32A/B/C (TO-220), BD135/137/139 and BD136/138/140 by gain group (TO-225, TO-126), 2N3055 and MJ2955 (TO-3) power transistors (17) | TIP31A/D, BD135/D, BD136/D, 2N3055/D | hFE at the datasheet's currents through a collector resistor; VCE(sat); VBE(on) |
| `darlington_onsemi_tip120`, `darlington_onsemi_tip125` | onsemi TIP120/121/122 and TIP125/126/127 Darlingtons (6): the datasheet's circuit, two transistors, the 8 kohm and 120 ohm shunts and the diode | TIP120/D | hFE at 0.5 A and 3 A; VCE(sat) at 3 A, 12 mA; VBE(on) |
| `bjt_onsemi_2n4401`, `bjt_onsemi_2n4403`, `bjt_onsemi_p2n2222a`, `bjt_onsemi_mpsa42`, `bjt_onsemi_mpsa92`, `bjt_onsemi_bc327` | onsemi 2N4401, 2N4403, P2N2222A, MPSA42 and MPSA92 (300 V) and BC327 by gain group, TO-92 (8) | 2N4401/D, 2N4403/D, P2N2222A/D, MPSA42/D, MPSA92/D, BC327/D | hFE at the datasheet's currents; VCE(sat); VBE(sat) or VBE(on) |
| `regulators_mc7800` | onsemi MC7805 to MC7824 in the B, C and AC grades (21), TO-220 | MC7800/D | VO within the 25 C limits at the table's Vin and 500 mA, and at 1.0 A; the 2.0 V dropout; quiescent current under its maximum |
| `resistors_ccf55` | Vishay CCF55, 1 %, 100 ppm/K: every E96 value from 10 ohm to 3.01 Mohm (527) | 31015 | the nominal resistance (a divider read to 0.1 %) |
| `resistors_ccf07` | Vishay CCF07, E24 from 10 ohm to 1 Mohm at 2 % and 5 %, and 1.1 to 2 Mohm at 5 % (249) | 31013 | the same |

| `capacitors_uvr` | Nichicon UVR, 85 C general purpose: every part number of its table, 6.3 to 450 V, 1 to 22000 uF (111) | UVR catalog | the RC time constant (1 - 1/e at 10 ms through 10 ms / C) and the ESR step |
| `capacitors_uhe` | Nichicon UHE, 105 C low impedance: every part number of its table, 6.3 to 100 V (212) | UHE catalog | the same |

| `capacitors_vishay_k` | Vishay K series radial leaded MLCC: every ordering code of its C0G and X7R tables, 10 pF to 1 uF, 50 to 500 V (385) | 45171 | the RC time constant |

The UVR's ESR is its catalog's tan δ at 120 Hz over the capacitive reactance;
the UHE's, its maximum impedance at 100 kHz, where a low-impedance
electrolytic is its ESR. The K series' ordering codes leave the tolerance and
lead options open (#): the family fills them with J (5 %) for C0G and K (10 %)
for X7R, bulk, and the datasheet's preferred L2 or H5 leads (K104K15X7RF53L2).
The bench draws each can at its catalog's diameter
and length with its lead spacing.

The resistors' part numbers are the datasheets' own coding (`CCF55` + the value
with R, K or M for its decimal point + the tolerance and temperature
coefficient letters + `E36`: CCF5510K0FKE36), and the bench marks them with
the code the datasheet names: five bands for the 1 % CCF55, four for CCF07.

## Interactive library batch

The library now includes 164 components, 44 named entries and 410 component
fixtures. The [interactive library record](../docs/engineering/interactive-library.md)
lists the 18 newly named controls, environmental/optical parts, optocouplers,
relays and connectors, with four live GUI/CLI circuit examples and exact model
limits. The explorer adds grouped categories, manufacturer/package/model filters
and Overview/Pins/Models previews before placement.

## First named-part batch

| Category | Parts | Implemented scope |
|---|---|---|
| Diodes | Vishay 1N4148, 1N4007; Nexperia BAT54; Vishay BZX55C5V1 | Forward/reverse DC behavior; Zener clamp; no charge storage/recovery |
| Transistors | onsemi 2N3904, 2N3906, BC547B, BC557B, 2N7000, BSS138; Infineon IRLZ44N | BJT Ebers-Moll and MOS level-1 approximations; cutoff/on fixtures; no MOS body diode or thermal behavior |
| Power | ST L7805CV, L7809CV, L7812CV; TI LM1117IMP-3.3 | Fixed regulation and dropout under bounded loads; no thermal shutdown, capacitor/ESR stability or startup guarantees |
| Amplifiers | TI LM358P | Both PDIP-8 channels; DC gain, finite output resistance, output headroom; no GBW/slew or common-mode violation model |
| Comparators | TI LM393P | Both PDIP-8 channels, externally pulled-up open collectors; no propagation delay, offset or input common-mode enforcement |
| Timers | TI NE555P | All eight terminals, divider/control voltage, latch, reset, discharge; bistable and RC astable fixtures; sampled switching, not transistor-level timing |
| Sensors | TI LM35DZ; Analog Devices TMP36GT9Z | Interactive ambient temperature -> nominal output voltage; endpoints and temperature changes; no thermal dynamics or accuracy-grade simulation |
| Logic | TI SN74HC00N, SN74HC02N, SN74HC04N, SN74HC08N, SN74HC32N, SN74HC86N | Every gate in the real 14-pin package; independent channels, four-state logic, nominal 12 ns inertial delay, rail sensing and 5 V electrical fixtures |

There are 26 named entries in this batch, plus a generic open-collector
comparator channel used by LM393P. No fake sensor protocol or unsupported MCU
family is advertised by adding a symbol.

Search by part number, manufacturer, package or keywords in the workbench.
The inspector shows the selected package, datasheet link, pin table and fidelity.
Original package illustrations are for the board view, **not PCB footprints or
mechanical drawings**. No KiCad symbol artwork or manufacturer photographs are
copied. DO-35/DO-41 terminal numbers use the ECAD convention 1=cathode,
2=anode; the band identifies the cathode on the physical unnumbered part.
TO-220 tabs duplicate pin 2. The LM1117 SOT-223 tab duplicates OUT (pin 2).

## Evidence and source status

For the first batch, manufacturer source review covers 25 of its 26 entries, with
exact PDF revisions, SHA-256 hashes, dates and page references in each entry's
`provenance.sources`. See [the review record](../docs/engineering/named-part-sources.md)
for the checked package maps, selected operating facts and unresolved sources.
TMP36GT9Z's TO-92 map is confirmed by Analog Devices Figure 4, page 1. Nexperia
BAT54 remains reference-only because its original PDF URL is unavailable and
alternative manufacturer requests return CDN Access Denied responses. The cloud
proxy now permits manufacturer access; this remaining failure is at the source.

The named library retains its own original symbols and artwork. KiCad family
references remain supplementary, pinned to revision
`4e17d7595f9691d45d97aea8ad64572652fb5bed`. The legacy TMP36xS symbol is SOT-23
and is not evidence for the TO-92 map.

Source review does not qualify the electrical models. Existing generic model
parameters were reused where appropriate; BAT54, BZX55C5V1 and BSS138 use
explicitly approximate nominal fits. The 1N4007 uses an illustrative 7 nA / n=1.8
forward fit instead of the legacy rectifier preset's implausibly low forward
curve. None is presented as an extracted manufacturer model. Electrical
qualification still needs operating points, tolerances and independent results.

## Run the component fixtures

From the repository root after building the engine:

```sh
python3 tools/validate.py --all
python3 tools/catalog/run_fixtures.py --embsim build/dev/bin/embsim
python3 tools/catalog/run_fixtures.py --embsim build/dev/bin/embsim --named
python3 tools/catalog/run_fixtures.py --embsim build/dev/bin/embsim --component ne555p
```

The runner converts each component fixture into a temporary project and scenario,
executes the actual CLI, checks every assertion, and fails on solver errors or
an empty selection. It preserves source fixtures. Analog waveform checks sample
at 1 MHz; functional/timed digital checks observe changes. A fixture requiring
faster analog sampling needs a more explicit rate contract before being added.
CTest runs all component fixtures on every core CI platform. This complements
schema checks and the engine’s `embsim catalog validate catalog` command.

## Timing and operating boundaries

- HC packages model nominal 5 V operation. Their 12 ns delay is a convenient
  nominal model value, not a datasheet maximum. Output amplitude does not track
  arbitrary supply voltage; power ramp, clamp diodes and supply current are absent.
- NE555 defaults to a 1 us threshold sampling bound. State changes commit only
  after an accepted solve and drive the following solve; event timing can be
  up to twice `sample_interval` late. Reduce it for faster circuits and establish
  convergence. The default is intended for low-frequency experiments. It is not
  a substitute for the part's maximum-frequency/propagation specifications.
- LM35DZ's exposed temperature range is 0..100 °C with a 4..30 V supply;
  TMP36GT9Z is -40..125 °C with a 2.7..5.5 V supply. Supply endpoint fixtures
  check nominal transfers; the TMP36 shifted-ground fixture checks both the
  absolute output voltage and its reported voltage relative to the sensor ground.
  Noise, self-heating, response time, calibration and supply-current effects are absent. LM35 negative-temperature circuitry is
  not modeled. LM35DZ at 0 °C is ideal extrapolation: the real near-ground
  output behavior is not qualified. Inputs outside the declared ranges are unsupported.
- LM393P outputs require external pull-ups and load-dependent voltage checks.
  An unloaded logic-high expectation is not valid when the output also drives a
  transistor base through a resistor.

See [the temperature alarm example](../examples/temperature-alarm/README.md)
and [the SDK](../sdk/README.md) for circuit and contribution workflows.

## Next qualification and expansion work

Recover manufacturer references for the six unreviewed named entries, establish
independent electrical reference curves and operating corners, and extend thermal,
spectral and switching behavior where required. Further families can add analog
switches, power converters and interface transceivers. Add each as a validated
behavioral batch with source evidence, not metadata alone.
