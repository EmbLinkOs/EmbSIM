# PLT - Platforms and Packaging

Group `PLT`. Design: ADR-008, ADR-013; 05 §3.4.

Each requirement has a statement, a rationale, a status (Accepted, FUTURE),
a verification method and a trace to the design. Verification names the
test or review that establishes it; STATUS.md records whether it passed.

---

### PLT-001  Three operating systems in CI
**Statement.** Linux, macOS and Windows shall build and test the core, the CLI and the workbench on every change.
**Rationale.** Required targets.
**Status.** Accepted 2026-10-15
**Verification.** CI matrix.
**Trace.** ADR-013

### PLT-002  Installers from CI
**Statement.** Desktop installers (AppImage, deb, dmg, msi) shall be produced by CI from one configuration by M2.
**Rationale.** Distribution.
**Status.** Accepted 2026-10-15
**Verification.** Release workflow artifacts.
**Trace.** 05 §3.4

### PLT-003  Offline operation
**Statement.** The desktop app and the browser mode shall function without network access.
**Rationale.** Engineering environments.
**Status.** Accepted 2026-10-15
**Verification.** Playwright run with network disabled.
**Trace.** 05 §3.4

### PLT-004  Minimum platform versions
**Statement.** Ubuntu 22.04, macOS 13 and Windows 10 shall be the minimum supported platforms for 1.0.
**Rationale.** 08 Q10.
**Status.** Accepted 2026-10-15
**Verification.** CI runners pinned accordingly.
**Trace.** 08 Q10

