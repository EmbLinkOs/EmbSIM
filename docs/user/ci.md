# EmbSIM in CI

A firmware's CI can run it on the simulator as an emulated target: its
boards, its circuit and its firmware images, headless and deterministic, in
the time the firmware needs rather than wall time. This page is the CI
contract (SPEC-008 §8, §9; SPEC-012 §4) and how to use it from GitHub
Actions, GitLab CI and Python.

## The contract

```sh
embsim run project.json --scenario scenarios/press.json --json \
    --record press.rec \
    --coverage coverage.info --coverage-format lcov \
    --profile profile.folded --profile-format collapsed > report.json
```

| Exit code | Meaning |
|---|---|
| 0 | every assertion passed |
| 1 | an assertion failed (or `--until-text` never appeared) |
| 2 | usage or load error: a missing project, image or board |
| 3 | a fatal diagnostic ended the run |
| 4 | `embsim replay`: the run diverged from its recording |

- **The report** (`--json`, on stdout) lists the assertions with their
  observed and expected values, the diagnostics, the statistics and, with
  the analyses on, the firmware's coverage and profile summaries.
- **The recording** (`--record`) holds the run's inputs and observations;
  `embsim replay press.rec` reruns it and stops at the first divergence
  (exit 4). A recording made on one machine replays the same on another:
  this is the regression test for the simulator itself.
- **Coverage** as lcov is what `genhtml`, Codecov and Coveralls read; the
  **profile** as collapsed stacks is what `flamegraph.pl` and speedscope
  read ([cli](cli.md#coverage-and-profile)).
- `tools/verify/compare_reports.py old.json new.json` diffs two reports,
  ignoring wall time, for a firmware change's review.

Runs are deterministic: the same project, images and scenario give the same
report and the same recording on every host and every run.

## An emulated target for a test runner

A test suite that prints its results on a console (an RTOS's conformance
tests, a library's unit tests built for the target) needs no project at all:

```sh
embsim run --target emblink/arduino_uno --image build/tests/test_sched.elf \
    --until 60s --until-text EMB_TEST_END --console
```

`--target` names a board of the catalog (`embsim board list`), `--image`
the ELF or HEX it runs, `--console` streams the board's console to stdout
as the run goes, and `--until-text` ends the run at the millisecond that
text appears, so a suite that finishes in 30 ms of target time takes about
that much wall time too. The runner then reads the console as it reads any
emulator's. EmbLinkRTOS runs its Arduino Uno conformance suite this way
(`tools/embsim/run_embsim.py` in EmbLinkRTOS, `EMB_TEST_EMULATOR=embsim`).

With a project, `--image B1=build/app.elf` replaces a board's image, so the
same project tests each build.

## GitHub Actions

Build EmbSIM from source (a minute on the hosted runners; cache it by the
commit you pin), build the firmware, run the scenarios, keep the artifacts:

```yaml
jobs:
  simulate:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - uses: actions/checkout@v4
        with:
          repository: EmbLinkOs/EmbSIM
          ref: main            # or a release tag
          path: embsim
      - name: Build EmbSIM
        run: |
          sudo apt-get update && sudo apt-get install -y ninja-build
          cmake -S embsim -B embsim/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEMBSIM_SERVER=OFF
          cmake --build embsim/build --target embsim_cli
          echo "$PWD/embsim/build/bin" >> "$GITHUB_PATH"
      - name: Build the firmware
        run: make -C firmware
      - name: Run the scenarios
        run: |
          for s in scenarios/*.json; do
            n=$(basename "$s" .json)
            embsim run project.json --scenario "$s" --json --record "out/$n.rec" \
              --coverage "out/$n.info" --coverage-format lcov > "out/$n.json"
          done
      - uses: actions/upload-artifact@v4
        if: always()
        with:
          name: embsim
          path: out/
```

Each `embsim run` exits nonzero when an assertion fails, so the step fails
with it; `if: always()` keeps the reports and recordings of a failed run.
`out/*.info` can go to a coverage service as they are.

## GitLab CI

```yaml
simulate:
  image: ubuntu:24.04
  script:
    - apt-get update && apt-get install -y git cmake ninja-build gcc
    - git clone --depth 1 https://github.com/EmbLinkOs/EmbSIM.git embsim
    - cmake -S embsim -B embsim/build -G Ninja -DCMAKE_BUILD_TYPE=Release
    - cmake --build embsim/build --target embsim_cli
    - embsim/build/bin/embsim run project.json --scenario scenarios/press.json --json --coverage coverage.info --coverage-format lcov > report.json
  artifacts:
    when: always
    paths: [report.json, coverage.info]
```

## From Python and pytest

The `embsim` package (`pip install ./python` from the EmbSIM repository)
wraps both: `embsim.run(...)` is the command line with its exit code and
report, and `embsim.Session` drives a session step by step, for tests that
react to what the firmware does ([python/README.md](../../python/README.md)).

```python
def test_button(embsim_session):
    s = embsim_session("project.json")
    s.advance(until="20ms")
    s.press("SW1")
    s.advance(by="5ms")
    assert "pressed" in s.serial("B1")
```
