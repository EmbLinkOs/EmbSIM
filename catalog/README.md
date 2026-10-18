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
