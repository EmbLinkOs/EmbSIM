# ADR 0001: portable kernel and shared clients

Status: accepted for the foundation; revisit using measured integration results.

Use a C99 digital kernel with no third-party runtime dependencies. This aligns
with reusable EmbCC implementations and supports native embedding. Keep an opaque
session API with explicit ownership and error returns. Use Python's standard
library for initial description validation, CLI, and development HTTP interface.
Use plain browser assets for the first GUI to avoid a frontend dependency stack
before interaction contracts stabilize.

Tradeoffs: Python becomes a prerequisite for current clients; ctypes requires
ABI discipline; the development UI is not yet a distributable desktop product.
The first kernel uses bounded arrays and a sorted event list for simplicity.
Replace the queue only when benchmarks justify it, retaining ordering tests.

Do not select an analog solver, plugin sandbox, or GUI framework by implication.
Those require separate decisions and acceptance experiments.
