# embsim for Python

The EmbSIM simulator from Python: a `Session` over its protocol (SPEC-008),
headless runs through the command line (`run`, `replay`), and pytest
fixtures. Standard library only; Python 3.10+.

```sh
pip install ./python            # from the repository; pytest extras: ./python[pytest]
export EMBSIM=build/release/bin/embsim   # or put embsim on the PATH
```

## A session

```python
from embsim import Session

with Session("examples/bluepill-button-led/project.json") as s:
    s.advance(until="20ms")                 # virtual time, deterministic
    assert "Blue Pill" in s.serial("B1")    # what the firmware wrote to its console
    s.press("SW1")
    s.advance(by="5ms")
    assert "pressed" in s.serial("B1")
    print(s.net("BTN"), s.output("B1.U1")["pc"])
```

| Method | Protocol |
|---|---|
| `advance(by=, until=)`, `reset()`, `now` | `control.advance`, `control.reset`, `session.info` |
| `set(target, input, value, at=)`, `press`, `release`, `send(target, text\|bytes)`, `inputs()` | `input.*` |
| `snapshot()`, `output(component)`, `net(name)`, `serial(board, unit=)`, `diagnostics()` | `observe.snapshot`, `diagnostics.list` |
| `probe(target, kind=)`, `read(probe)` | `probe.add`, `probe.read` |
| `run_scenario(path\|dict)` | `scenario.run` (the SPEC-008 §8 report) |
| `record()`, `stop_recording()`, `replay(rec)`, `to_scenario(rec)` | `record.*` (SPEC-012) |
| `analyze()`, `coverage(format=)`, `profile(format=)` | `analysis.*` (lcov, collapsed stacks or JSON) |
| `where`, `registers`, `memory`, `breakpoint` | `debug.*` |
| `call(method, **params)` | any method |

An error from the simulator raises `EmbsimError` (its JSON-RPC `code` and
`message`).

## Headless runs

`run()` is `embsim run --json`, the CI contract (SPEC-012 §4):

```python
import embsim

r = embsim.run("examples/bluepill-button-led/project.json",
               scenario="examples/bluepill-button-led/scenarios/press.json",
               record="press.rec", coverage="cov.info", profile="prof.folded")
assert r.ok, r.failed          # the assertions that did not pass
print(r.coverage["B1"]["lines"])
assert embsim.replay("press.rec").code == embsim.EXIT_OK
```

Exit codes: `EXIT_OK` 0, `EXIT_ASSERTION` 1, `EXIT_USAGE` 2, `EXIT_FATAL` 3,
`EXIT_DIVERGED` 4.

## pytest

Installed, the package registers a fixture factory:

```python
def test_banner(embsim_session):
    s = embsim_session("examples/bluepill-button-led/project.json")
    s.advance(until="20ms")
    assert "Blue Pill" in s.serial("B1")
```

`pytest --embsim PATH` names the executable. `python/tests/test_session.py`
is the package's own test (`python_client` in CTest).
