# Named package geometry evidence

Reviewed 2026-10-18. This extends the [named-part source review](named-part-sources.md)
with representative bench bodies. It does not qualify manufacturing footprints,
electrical models or every package offered under a family name. No vendor PDF or
illustration is redistributed; the procedural geometry is authored in EmbSIM.

| Entry | Body and pin identity | Geometry evidence and limits |
|---|---|---|
| BSS138 | SOT-23: G1, S2, D3; pin 1 bottom-left in the top view | [onsemi BSS138/D](https://www.onsemi.com/pdf/datasheet/bss138-d.pdf), April 2024 rev. 7, PDF page 6: CASE 318 issue AU, 14 Aug 2024. Nominal body 2.90 × 1.30 mm, overall lead span 2.40 mm, height 1.00 mm; pin-1/2 pitch 1.90 mm. Nominal lead width 0.44 mm, thickness 0.14 mm. |
| LM1117IMP-3.3 | SOT-223: GND1, OUT2, IN3; tab/pin 4 is also OUT | [TI SNOS412Q](https://www.ti.com/lit/ds/symlink/lm1117.pdf), Jan 2023, PDF page 44: DCY0004A outline 4214843/C, May 2026. Body midpoint 6.50 × 3.50 mm, overall lead-span midpoint 7.00 mm, maximum height 1.80 mm, lead pitch 2.30 mm. Lead width/thickness use range midpoints 0.75/0.29 mm. The drawing is rotated so pin 1 is front-left. |
| BAT54 | Representative SOT-23 family body; A1, NC2, K3 | The existing KiCad reference pin map is retained. The outline uses the reviewed onsemi CASE 318 family geometry above, explicitly recorded as a family reference. Nexperia's BAT54 PDF remains unverified; the onsemi BAT54 URL retrieved HTML rather than a PDF during this review. No BAT54 manufacturer qualification is implied. |

The retrieved BSS138 and LM1117 PDFs have the same SHA-256 values recorded by the
2026-10-16 review. Their mechanical appendix revision dates are recorded separately
from the device datasheet revisions. Source records in each component contain the
reviewed pages, hashes and boundaries.

## Representation and connectivity

`bench.package` adds reusable `sot23` and `sot223` bodies. `size_mm` is the outer
representative envelope; the molded body is narrower than the full lead span.
Coordinates in `bench.pins` locate representative lead centers, not recommended
PCB land centers. Do not export these coordinates as a manufacturing footprint.
Bodies include flat metal feet, simplified gull-wing transitions and a pin-1 dot;
SOT-223 also has its metal output tab. Marking codes, mold details, tolerance
stacks, solder joints and every manufacturer's variant are not reproduced.

Canonical electrical terminals stay unchanged. In particular LM1117's tab is
rendered but not exposed as an invented fourth net or independently wireable
contact; OUT2 represents its shared conductor. BAT54's NC terminal stays NC.
The existing electrical approximation/fidelity declarations and fixtures remain.

Surface-mount packages can be placed and wired as abstract lead connections, but
they do not snap into, reserve or become electrically inserted into breadboard
holes through top-down overlap. The workbench explains that breadboard use needs
a carrier. A soldering workflow, PCB pad placement and carrier-board assembly are
not implemented by this milestone. BAT54 no longer receives a misleading TO-92
body even though its manufacturer source review remains open.

## Verification

`surface-packages.test.ts` checks pin-side ordering and datasheet pitches,
actual mesh envelopes against the package dimensions, unchanged electrical
terminal sets, and rejection of direct insertion even when a lead is positioned
exactly above a breadboard hole. The all-catalog shape fixture checks bounds and
pin coverage; schema/native catalog fixtures check existing circuit behavior.
Native gallery captures inspect bodies through `embsim serve` using isolated
components, not a powered or manufacturer-qualified test circuit.
