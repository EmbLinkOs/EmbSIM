# EmbSIM desktop shell

A Tauri 2 shell (ADR-008) around the workbench in `ui/`. It contains no
simulation: on start the workbench calls `start_sidecar`, the shell runs the
bundled `embsim serve [PROJECT] --port <free port>` and the workbench connects
to its WebSocket on loopback. Closing the window stops the engine.

## Build

Requirements: Rust 1.77+, Node 22 with pnpm, the platform webview (WebKitGTK
4.1 on Linux, WebView2 on Windows, WKWebView on macOS), and the engine built
in release mode.

```sh
cmake --preset release && cmake --build --preset release   # the engine
python3 desktop/prepare-sidecar.py                         # copy it as the sidecar
cargo install tauri-cli --version '^2'                     # once
cd desktop/src-tauri && cargo tauri dev                     # or: cargo tauri build
```

`cargo tauri build` produces AppImage and deb on Linux, dmg on macOS and msi
on Windows, bundling the engine and the `catalog/` library.

## Status

The shell has not been built in CI yet; the browser delivery (`embsim serve`)
runs the same workbench and is the tested path in M0 (see `docs/STATUS.md`).
