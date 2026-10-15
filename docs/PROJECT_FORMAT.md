# Project schema 1

The implemented schema is validated by `python/embsim/project.py`.
Unknown fields and unsupported versions/models are errors. Files are JSON;
identifiers begin with a letter and contain at most 64 ASCII letters, digits,
underscores or hyphens. Names are case-sensitive.

Root fields: `schema_version` (integer 1), `name` (identifier), `nets` (unique net
identifiers), `components` (unique component objects), optional `events` (list).

| Component kind | Required fields besides `id`, `kind` | Optional parameters | Semantics |
|---|---|---|---|
| `button` | `net` | `active_level`: 0 or 1, default 1 | Strong drive while pressed; Z while released; initially released |
| `pull` | `net` | `level`: 0 or 1, default 0 | Constant weak digital driver; no resistance or current model |
| `indicator` | `net` | none | Ideal logic probe displayed as an LED; lit on 1, dark otherwise with logic state exposed |

Each event has `at_ns` (unsigned 64-bit integer), `component` (a button ID), and
`pressed` (JSON boolean). Events can appear out of timestamp order: the kernel
orders timestamps, preserving file order for ties. Live commands are appended
after existing equal-time events. Inputs are applied through the requested time,
inclusive. `Z` means no active driver, `X` means unknown/conflicting strongest
drivers. Strong drivers override weak pulls; opposing equal-strength drivers
resolve to X. Button bounce and propagation delays are not modeled.

Limits: 256 nets, 1024 components, 7000 initial events; kernel maximum 1024
drivers and 8192 pending events. These are foundation limits, not performance
claims. The API rejects past events and backwards advances. Schema 1 does not
describe real boards, packages, firmware or analog components yet.

## HTTP control API

The loopback development server starts paused. `GET /api/state` returns the same
simulation snapshot used by the CLI plus `running`. `POST /api/control` accepts
`Content-Type: application/json` and exactly one of:

```json
{"action":"set","component":"SW1","pressed":true}
```
```json
{"action":"advance","until_ns":20000000}
```
```json
{"action":"run"}
```

`pause` and `reset` use the same shape as `run`. Reset reloads the original project,
including its scheduled inputs. Unknown or invalid commands return HTTP 400.
The server is for local development; there is no authentication or remote access
mode. It accepts same-origin loopback requests only. No durable replay format is
defined by this diagnostic API.

Current browser controls use JavaScript numbers, so reliable numeric display and
manual stepping are limited to `2^53 - 1` ns. The native kernel/CLI accept full
unsigned 64-bit timestamps. A versioned string time representation is required
before the browser supports longer experiments.
