# Sidecar binaries

Tauri expects the engine here as `embsim-<target-triple>` (for example
`embsim-x86_64-unknown-linux-gnu`, `embsim-aarch64-apple-darwin`,
`embsim-x86_64-pc-windows-msvc.exe`). `desktop/prepare-sidecar.py` copies the
release build into place with the right name. Binaries are build outputs and
are not committed.
