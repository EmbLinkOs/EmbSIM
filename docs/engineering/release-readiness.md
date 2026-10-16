# Release 1.0 readiness

The release boundary is defined in [the roadmap](../architecture/07-roadmap.md).
This is a working checklist, not a release announcement. Version numbers must
not imply electrical qualification or peripheral support that has not been tested.

## Integration workflow

Start each focused change from freshly fetched `origin/main`. Keep unrelated
contributor branches intact. Before submitting, fetch again and integrate new
main changes, then rerun affected checks. Commit using the owner's configured
identity. Keep private agent instructions and environment files outside the project.

## Release gates

| Area | Evidence required before release |
|---|---|
| Engine | Development and sanitizer suites; deterministic replay; analytic and independently generated analog comparisons with tolerances and documented model limits |
| Firmware | A board/peripheral support matrix tied to executable firmware cases; unsupported accesses visible to users; acceptance circuits running through CLI and GUI |
| Workbench | Real-engine design, edit, save/reopen, run, interact, debug and verify flows; keyboard and accessibility checks; both themes and compact screens |
| Multiple boards | Board and core selection isolates debugger data; UART interaction and replay agree across runs |
| Catalog | Every declared policy instantiates; fixtures execute; manufacturer facts and behavioral approximations distinguished; independent SDK contribution evidence |
| Distribution | Linux, macOS and Windows native builds and desktop installation checks; signing, upgrade and support policy documented |
| Performance | Reproducible representative circuit and large schematic benchmarks with hardware, configuration and thresholds recorded |
| Documentation | Getting started from a clean installation, accurate support matrix, known limitations, release notes and migration instructions |

## Current hardening slice

Debugger board selection clears memory, source, peripherals, register comparisons
and line-breakpoint presentation. Responses and errors from a previous selection
are discarded, including switching away and back before a request finishes.
The busy indicator remains active until all requests for the current selection
finish. Regression tests exercise delayed requests and overlapping operations.

This slice does not complete the other gates. The status inventory contains
historical entries; reconcile them with current code and fresh evidence before
using them as a release support claim. Track failures as concrete issues rather
than treating component counts or a passing schema check as accuracy evidence.
