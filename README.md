# EmbSIM

EmbSIM is an embedded systems simulator under development: one deterministic
engine for firmware, boards, circuits, command line automation, and graphical
interaction.

The long-term goal is to let engineers develop and investigate embedded systems
before hardware is available. Each model must declare what it implements and
how its behavior was validated. Functional simulation does not imply electrical
or cycle accuracy.

## Current milestone

The first implementation is a **digital interaction foundation**, not yet a
firmware or analog simulator. It contains a portable C99 event/net kernel,
JSON project descriptions, ideal buttons and LED indicators, a CLI, and a local
browser interface. Both interfaces use the same project runtime. Native firmware
execution, resistors, transistors, analog solving, and MCU pin multiplexing are
planned; they are not implemented here yet.

Targets: **Linux, macOS, and Windows**. See [platform status and setup](docs/PLATFORMS.md).
Requirements: a supported C compiler and Python 3.10+; no Python
packages or network services are needed.

```sh
python scripts/build.py --test
python scripts/embsim.py run examples/button-led.json --until-ns 20000000
python scripts/embsim.py serve examples/button-led.json
```

The last command starts a local development UI on port 8765. Its button events
are timestamped at the current simulation time. Run/pause controls affect virtual
time; reset reconstructs the project and clears its trace. The CLI emits JSON
containing virtual time, net states, component states, and external input events.
The example also contains a scheduled button press and release for automation.

Read [the product specification](docs/SPECIFICATION.md),
[architecture](docs/ARCHITECTURE.md), [roadmap](docs/ROADMAP.md),
[project format](docs/PROJECT_FORMAT.md), and
[existing implementation assessment](docs/EXISTING_WORK.md) before extending it.

## Development

`python scripts/build.py --test` (or `make test` on Unix) builds the kernel with warnings treated as errors and runs tests for
event ordering, time boundaries, digital contention, description validation,
CLI behavior, and HTTP controls. `make clean` removes generated build files.
Override `CC`, `CFLAGS`, or `PYTHON` on the Make command line as needed.

An optional Chromium/Playwright interaction check is available with
`python tests/browser_smoke.py`; set `EMB_BROWSER` to a system Chromium executable
if Playwright-managed browsers are not installed. It exercises desktop/mobile
controls and writes screenshots to ignored `build/browser/`.

Source is licensed under Apache-2.0. See [LICENSE](LICENSE).
