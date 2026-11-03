# AI assistants

An AI assistant can use EmbSIM the way you do (ADR-035). `embsim-mcp` is a
[Model Context Protocol](https://modelcontextprotocol.io) server: it runs one
EmbSIM session and gives the assistant the operations you have in the
workbench and the command line, through the same session protocol. The
assistant sees what you would see (the parts' outputs, the nets, the serial
consoles, the diagnostics) and nothing more; it cannot reach past the models.

## Installing

`embsim-mcp` comes with the Python client:

```sh
pip install ./python          # from a checkout; adds the embsim-mcp command
embsim-mcp --help
```

It needs only Python 3.10 and an `embsim` executable: `--embsim PATH`,
`$EMBSIM`, `embsim` on the PATH, or a build in the checkout it sits in.
`--project PROJECT` loads a project at the start.

## Connecting an assistant

Any MCP client that starts a server on stdio can use it. For Claude Code:

```sh
claude mcp add embsim -- embsim-mcp --embsim /path/to/embsim
```

For Claude Desktop and other clients configured with a JSON file:

```json
{
  "mcpServers": {
    "embsim": {
      "command": "embsim-mcp",
      "args": ["--embsim", "/path/to/embsim", "--project", "/path/to/project.json"]
    }
  }
}
```

Without installing, `python -m embsim.mcp` with `PYTHONPATH` set to the
checkout's `python` directory does the same.

## What the assistant can do

| Tool | What it does |
|---|---|
| `session_info` | the engine, the project, its boards and their firmware, the time |
| `project_new`, `project_load`, `project_get`, `project_save`, `project_validate` | start, open, read, save and check a project |
| `project_edit` | add and remove parts, set parameters, connect and disconnect pins, rename nets, add boards, set layout: one transaction, all or nothing |
| `catalog_search`, `catalog_get` | find parts and boards, read a part's pins, parameters and fidelity |
| `firmware_build` | build a board's firmware with its toolchain profile |
| `run`, `reset` | run by a duration or until a time (simulated), reset to power-on |
| `inputs_list`, `press`, `release`, `set_input`, `serial_send` | work buttons, switches, pots and sensors; type into a serial port or a terminal |
| `serial_read`, `observe`, `probe_add`, `probe_read`, `diagnostics` | read the consoles, the nets and outputs, attach probes, see the warnings |
| `scenario_run`, `record_start`, `record_stop` | run a scenario's inputs and assertions; record a session |
| `debug_registers`, `debug_memory` | a board's registers and memory |

A refused operation (a pin that does not exist, a part over its rating
refused by the policy) comes back as the tool's error, with the session's own
message, as the workbench would show it.

## An example

Asked to light an LED from 5 V and check its current, an assistant calls:

1. `catalog_search` with `query: "led"`, then `catalog_get` on `emblink/led`;
2. `project_new` with `name: "led"`, `policy: "electrical"`;
3. `project_edit` with the parts (`emblink/vsource` at 5 V, a 330 ohm
   `emblink/resistor`, `emblink/led`, `emblink/gnd`) and their connections;
4. `run` with `by: "5ms"`;
5. `observe`: the LED's current, about 10 mA, and the resistor's as drawn
   (its tolerance applies, as a real resistor's does).

`python/tests/test_mcp.py` runs exactly this, and the Blue Pill example's
firmware, as its tests.
