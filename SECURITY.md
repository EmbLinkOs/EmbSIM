# Security policy

EmbSIM is pre-release; there is no supported version yet.

Report a vulnerability privately through GitHub's "Report a vulnerability"
form on this repository, or to the repository owner by email as listed on
their GitHub profile. Include the source revision, the platform, a
reproduction, and the impact. Expect an acknowledgement within seven days.

Scope: the `embsim` command line and `libembsim` (parsers for JSON, ELF, HEX
and register files, the WebSocket and stdio protocol endpoints, the GDB
stub), the workbench, and the catalog's scripted models. The server binds
loopback by default and has no authentication; exposing it on a network is
unsupported. Native plugins are trusted code by design (ADR-009).
