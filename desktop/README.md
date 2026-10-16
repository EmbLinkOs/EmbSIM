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
cd desktop && npx @tauri-apps/cli@2 dev                    # a window on the Vite dev server
cd desktop && npx @tauri-apps/cli@2 build                  # the installers
```

`tauri build` produces AppImage and deb on Linux, dmg on macOS and msi on
Windows, bundling the engine and the `catalog/` library. CI builds all four
on every pull request and keeps them as artifacts.

`embsim-desktop PROJECT.json` opens a project at start; otherwise the
workbench opens one from its file dialog. The workbench's errors are
printed on the shell's standard error (the webview's console is not
visible). The engine accepts the webview's connection because the shell
starts it with `--allow-origin` for the webview's origin (SPEC-008 §1).

`make-icons.py` draws the application icon into `src-tauri/icons`.

## Status

Built and run on Linux (WebKitGTK 4.1, under Xvfb) in this workspace: the
window shows the workbench with the engine's project loaded. macOS and
Windows builds are CI's; see `docs/STATUS.md` for what has been observed.
