# SPEC-010 - Toolchain Profiles

**Status:** Accepted in outline 2026-10-15; completed before M1. **Builds
on:** 04 §9; ADR-010. **Schema:** `schemas/toolchain.schema.json`.
**Requirements:** `docs/requirements/TCH.md`.

---

## 1. Profile description

```json
{
  "schema_version": 1,
  "kind": "toolchain",
  "id": "gcc-arm",
  "name": "GNU Arm Embedded (arm-none-eabi-gcc)",
  "detect": {"command": ["arm-none-eabi-gcc", "--version"], "version_regex": "\\) ([0-9.]+)"},
  "targets": {
    "cortex-m3": {"cc": ["arm-none-eabi-gcc", "-mthumb", "-mcpu=cortex-m3"], "link_flags": ["-nostartfiles", "-Wl,--gc-sections"]},
    "cortex-m4f": {"cc": ["arm-none-eabi-gcc", "-mthumb", "-mcpu=cortex-m4", "-mfloat-abi=hard", "-mfpu=fpv4-sp-d16"]}
  },
  "steps": {
    "compile": ["${cc}", "-g", "-O2", "-ffunction-sections", "-fdata-sections", "-c", "${source}", "-o", "${object}"],
    "link":    ["${cc}", "${link_flags}", "-T${linker_script}", "${objects}", "-o", "${image}"],
    "convert": null
  },
  "outputs": {"image": "elf", "debug": "dwarf"},
  "environment": {"PATH_hint": ["/usr/bin", "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/*/bin"]}
}
```

Profiles shipped in `catalog/toolchains/`: `embcc`, `gcc-arm`, `gcc-riscv`,
`gcc-avr`, `clang`, `xc8`, `xc16`, `xc32`. Boards contribute per-profile
target settings (SPEC-006 §3 `toolchains`); a project names the profile and
the sources.

## 2. Execution

`toolchain.build` and `embsim build` expand templates (`${...}` from the
profile, the board, the project and the environment), run each step with
the host layer's process API, capture output, and on success set the
project's image. Nothing is executed without the user's request; profiles
never run at load. Detection runs on request (`toolchain.detect`) and caches
results per session.

As implemented: the board's settings for the profile name its target by
`target` (a triple: clang, EmbCC) or by `cpu` (the GNU and XC profiles key
their targets by CPU). The variables:
- lists, spliced where an argument is exactly the variable: `${cc}`,
  `${objcopy}` (the target's), `${cflags}` (the board's `flags` then the
  project's), `${link_flags}` (the target's then the project's firmware
  `link_flags`, applied to the link alone: a freestanding program's
  `-nostartfiles`), `${link}` (the board's), `${defines}` (the target's,
  written as flags, then the project's as `-D`), `${include}`,
  `${objects}`;
- single values, replaced inside an argument: `${source}`, `${object}`,
  `${image}`, `${map}` (the image's path with `.map` after it), `${hex}`
  (with `.hex` for its extension), `${linker_script}` (the project's, else
  the board's; an argument naming it is dropped when there is none),
  `${target}`, `${cpu}`.

## 3. XC specifics

`xc8-cc` produces HEX and, for recent versions, ELF with partial DWARF; the
profile declares `outputs.image: "hex"` and `debug: "none"` until a PIC16/18
core exists. `xc16-gcc` and `xc32-gcc` produce ELF with DWARF and HEX through
`xc16-bin2hex`/`xc32-bin2hex`; the profile's `convert` step runs them when
the board prefers HEX. Device packs (`--dfp`) are supplied through the
profile's `environment.DFP` and passed to the MCU's register-file lookup.

## 4. TinyCC and EmbCC on the host

TinyCC builds EmbSIM's core and CLI (`make CC=tcc`); EmbCC builds them with
its Linux host target when installed. Neither is a firmware profile for
EmbSIM's targets except EmbCC, which is a profile for every target it
supports.
