# Named-part manufacturer source review

Scope: the first 26-entry named-part batch. The [interactive follow-up](interactive-library.md)
records later entries separately.

Reviewed 2026-10-16, after merging the M4 runtime work and the first named-part
batch. This records package identity, pin functions and selected operating facts,
not vendor approval or electrical model qualification. No manufacturer PDFs,
drawings or text excerpts are redistributed in the repository. Each reviewed
entry's first `provenance.sources` item records the retrieved PDF's SHA-256,
revision, URL, review date and page references. Hashes identify the bytes reviewed;
they are not vendor signatures. Manufacturer links may serve newer revisions.

25 of 26 named entries were reviewed from 23 distinct PDFs (the three L78 entries
share one document). All reviewed package maps agree with the current catalog.
TMP36's TO-92 map is now confirmed directly by the manufacturer, replacing the
earlier reference-only status. Source review does not remove any documented
electrical omission or turn illustrative fits into extracted models.

| Catalog entry | Manufacturer document | Revision | PDF pages | Checked facts and boundaries |
|---|---|---|---|---|
| `bc547b` | [BC546/D](https://www.onsemi.com/pdf/datasheet/bc546-d.pdf) | 8, August 2021 | 1, 7 | TO-92 case 29 style 17: C1, B2, E3. |
| `bc557b` | [BC556B/D](https://www.onsemi.com/pdf/datasheet/bc556b-d.pdf) | 3, March 2007 | 1, 8 | TO-92 case 29 style 17: C1, B2, E3. |
| `bjt_2n3904` | [2N3903/D](https://www.onsemi.com/pdf/datasheet/2n3903-d.pdf) | 9, August 2021 | 1, 8 | TO-92 case 29 style 1: E1, B2, C3; family document explicitly includes 2N3904. |
| `bjt_2n3906` | [2N3906/D](https://www.onsemi.com/pdf/datasheet/2n3906-d.pdf) | 4, February 2010 | 1, 8 | TO-92 case 29 style 1: E1, B2, C3. |
| `bss138` | [BSS138/D](https://www.onsemi.com/pdf/datasheet/bss138-d.pdf) | 7, April 2024 | 1 | SOT-23-3: G1, S2, D3. |
| `bzx55c5v1` | [85604](https://www.vishay.com/docs/85604/bzx55.pdf) | 2.0, 7 November 2024 | 1, 2, 5 | DO-35, cathode band. C5V1 nominal 5.1 V, 4.8 to 5.4 V at 5 mA. A2/K1 numbering is an ECAD convention, not a manufacturer lead number. |
| `diode_1n4007` | [88503](https://www.vishay.com/docs/88503/1n4001.pdf) | 29 April 2020 | 1, 2 | DO-41, cathode band. A2/K1 numbering is an ECAD convention; no manufacturer-numbered axial leads. |
| `diode_1n4148` | [81857](https://www.vishay.com/docs/81857/1n4148.pdf) | 1.6, 7 November 2024 | 1, 2 | DO-35, black cathode band. A2/K1 numbering is an ECAD convention; no manufacturer-numbered axial leads. |
| `irlz44n` | [PD-94831](https://www.infineon.com/assets/row/public/documents/24/49/infineon-irlz44n-datasheet-en.pdf) | 11 November 2003 | 1, 2, 8 | TO-220AB: G1, D2, S3, tab D. Manufacturer-hosted legacy IRLZ44NPbF document; catalog retains the IRLZ44N family name. |
| `l7805cv` | [DS0422](https://www.st.com/resource/en/datasheet/l78.pdf) | 38, February 2025 | 3, 13 | TO-220: IN1, GND2, OUT3; tab GND. Review selects the 5 V L7805C table. |
| `l7809cv` | [DS0422](https://www.st.com/resource/en/datasheet/l78.pdf) | 38, February 2025 | 3, 17 | TO-220: IN1, GND2, OUT3; tab GND. Review selects the 9 V L7809C table. |
| `l7812cv` | [DS0422](https://www.st.com/resource/en/datasheet/l78.pdf) | 38, February 2025 | 3, 18 | TO-220: IN1, GND2, OUT3; tab GND. Review selects the 12 V L7812C table. |
| `lm1117_3v3` | [SNOS412Q](https://www.ti.com/lit/ds/symlink/lm1117.pdf) | January 2023 | 3 | SOT-223: GND1, OUT2, IN3; tab/pin 4 also OUT. The simulator represents that shared conductor by OUT2. |
| `lm358p` | [SLOS068AB](https://www.ti.com/lit/ds/symlink/lm358.pdf) | October 2024 | 3 | PDIP-8: OUT1=1, IN1-=2, IN1+=3, V-=4, IN2+=5, IN2-=6, OUT2=7, V+=8. Select legacy LM358, not LM358B electrical tables. |
| `lm35dz` | [SNIS159H](https://www.ti.com/lit/ds/symlink/lm35.pdf) | December 2017 | 1, 3, 4, 8, 23 | TO-92 bottom view: VS1, OUT2, GND3. LM35D grade 0 to 100 C; nominal 10 mV/C, 4 to 30 V supply. Ground-adjacent output behavior is not qualified. |
| `lm393p` | [SLCS005AH](https://www.ti.com/lit/ds/symlink/lm393.pdf) | April 2025 | 3 | PDIP-8: OUT1=1, IN1-=2, IN1+=3, GND4, IN2+=5, IN2-=6, OUT2=7, VCC8. Select legacy LM393, not LM393B electrical tables. |
| `mosfet_2n7000` | [2N7000/D](https://www.onsemi.com/download/data-sheet/pdf/2n7000-d.pdf) | 8, April 2011 | 2, 5 | TO-92 case 29 style 22: S1, G2, D3. PDF has an extra cover; heading specifies Pb-free 2N7000G. |
| `ne555p` | [SLFS022K](https://www.ti.com/lit/ds/symlink/ne555.pdf) | March 2026 | 3 | PDIP-8: GND1, TRIG2, OUT3, RESET4, CONT5, THRES6, DISCH7, VCC8. Catalog CTRL/THRESH are aliases of CONT/THRES. Supply 4.5 to 16 V for NE555. |
| `sn74hc00n` | [SCLS181H](https://www.ti.com/lit/ds/symlink/sn74hc00.pdf) | August 2021 | 3 | N/PDIP-14 pin table; four NAND channels; VCC14, GND7. |
| `sn74hc02n` | [SCLS076G](https://www.ti.com/lit/ds/symlink/sn74hc02.pdf) | December 2020 | 3 | N/PDIP-14 pin table; four NOR channels; output-first numbering differs from HC00; VCC14, GND7. |
| `sn74hc04n` | [SCLS078H](https://www.ti.com/lit/ds/symlink/sn74hc04.pdf) | April 2021 | 3 | N/PDIP-14 pin table; all six inverter channels; VCC14, GND7. |
| `sn74hc08n` | [SCLS081J](https://www.ti.com/lit/ds/symlink/sn74hc08.pdf) | February 2025 | 3 | N/PDIP-14 pin table; four AND channels; VCC14, GND7. |
| `sn74hc32n` | [SCLS200F](https://www.ti.com/lit/ds/symlink/sn74hc32.pdf) | April 2021 | 3 | N/PDIP-14 pin table; four OR channels; VCC14, GND7. |
| `sn74hc86n` | [SCLS100F](https://www.ti.com/lit/ds/symlink/sn74hc86.pdf) | April 2021 | 3 | N/PDIP-14 pin table; four XOR channels; VCC14, GND7. |
| `tmp36gt9z` | [TMP35/TMP36/TMP37](https://www.analog.com/media/en/technical-documentation/data-sheets/TMP35_36_37.pdf) | H, May 2015 | 1, 3, 19 | Figure 4 TO-92 bottom view: VS1, OUT2, GND3. TMP36GT9Z ordering code on page 19. Nominal 500 mV offset + 10 mV/C; -40 to 125 C; supply 2.7 to 5.5 V. |

## Outstanding source

`bat54` retains its original Nexperia identity and KiCad reference. Its advertised
`BAT54_SER.pdf` URL returned HTTP 404; alternative manufacturer document requests
returned the origin/CDN's Access Denied HTML. This is distinct from the earlier
cloud proxy CONNECT rejection: TLS connections to the host now succeed. No PDF
revision or hash is claimed for BAT54, and its A1/NC2/K3 map remains reference-only.
Do not substitute BAT54A/C/S dual-diode package maps for the single BAT54.

## Model follow-through

- The sensor supply envelopes now follow the selected datasheets: LM35DZ 4 to
  30 V, TMP36GT9Z 2.7 to 5.5 V. New fixtures exercise their supply endpoints with
  nominal transfer functions and a TMP36 with ground shifted above global zero.
  Passing those fixtures establishes ideal model behavior, not accuracy limits.
- LM35DZ models the LM35D temperature range, not the headline full-range LM35.
  Its 0 C endpoint is an ideal extrapolation; real near-ground output limitations
  and negative-temperature external bias circuitry remain unmodeled.
- NE555P uses the NE555 supply rating, not SE555's 18 V rating. Its sampled
  switching timing is unchanged and is not a datasheet propagation guarantee.
- LM358P and LM393P must use their original-device tables for later electrical
  qualification, rather than the B/BA variants in their combined documents.
- Axial diode lead numbers are an ECAD convention. The manufacturer evidence is
  package shape and cathode marking; it does not assign the simulator's 1/2 labels.
- Semiconductor charge, recovery, body diodes, thermal limits, parameter corners,
  sensor noise/accuracy and dynamic supply behavior remain as documented omissions.

The retained PDFs are a local reference cache outside the checkout, not a
dependency of the test suite. CI pin-map regressions use the reviewed package
facts; electrical fixtures run offline against the actual shared engine.

## Bench bodies reviewed 2026-10-18

[Package geometry evidence](package-geometry.md) records the mechanical appendix
review for BSS138/SOT-23 and LM1117IMP-3.3/SOT-223 and the representative-family
BAT54 body. The same retrieved PDF hashes were verified. This does not close
BAT54's missing Nexperia source review or any electrical model omission.

The 2026-10-18 package review also retrieved the independent onsemi BAT54/D
September 2017 rev. 3 through its official download endpoint. PDF page 2 confirms
A1/NC2/K3 for the single BAT54, separately from BAT54A/C/S. This corroborates the
reference map but does not close the selected Nexperia source gap or qualify
cross-manufacturer models. The BAT54 source record contains the hash and limits;
the PDF has no dimensioned mechanical outline.
