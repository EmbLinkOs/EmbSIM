// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Junior Deogracias
//! The EmbSIM desktop shell (ADR-008): a platform webview showing the
//! workbench, and the `embsim` engine as a sidecar process. The shell holds
//! no simulation; it starts `embsim serve` on a free loopback port and hands
//! the WebSocket URL to the workbench through the `start_sidecar` command.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::net::TcpListener;
use std::sync::Mutex;

use serde::Serialize;
use tauri::{Manager, State};
use tauri_plugin_shell::process::CommandChild;
use tauri_plugin_shell::ShellExt;

#[derive(Default)]
struct Sidecar {
    child: Mutex<Option<CommandChild>>,
    url: Mutex<Option<String>>,
}

#[derive(Serialize)]
struct SidecarInfo {
    ws_url: String,
}

/// A free loopback port: bind to port 0, read the port, release it.
fn free_port() -> Result<u16, String> {
    TcpListener::bind("127.0.0.1:0")
        .and_then(|l| l.local_addr())
        .map(|a| a.port())
        .map_err(|e| format!("no free loopback port: {e}"))
}

/// Starts `embsim serve [PROJECT] --port N` once and returns its WebSocket URL.
#[tauri::command]
fn start_sidecar(app: tauri::AppHandle, state: State<Sidecar>, project: Option<String>) -> Result<SidecarInfo, String> {
    if let Some(url) = state.url.lock().map_err(|e| e.to_string())?.clone() {
        return Ok(SidecarInfo { ws_url: url });
    }
    let port = free_port()?;
    let mut args: Vec<String> = vec!["serve".into()];
    // the workbench's choice, else the file the shell was opened with
    // (`embsim-desktop project.json`, or a file association)
    if let Some(p) = project.or_else(|| std::env::args().nth(1).filter(|a| !a.starts_with('-'))) {
        args.push(p);
    }
    args.push("--port".into());
    args.push(port.to_string());
    // the engine accepts WebSocket connections from its own pages only;
    // the webview's origin differs by platform (and is the dev server in
    // a debug build)
    let mut origins = vec!["tauri://localhost", "http://tauri.localhost", "https://tauri.localhost"];
    if cfg!(debug_assertions) {
        origins.push("http://localhost:5173");
    }
    for o in origins {
        args.push("--allow-origin".into());
        args.push(o.into());
    }
    let mut command = app.shell().sidecar("embsim").map_err(|e| format!("embsim sidecar missing: {e}"))?.args(args);
    // the catalog travels with the application as a resource; an installed
    // engine does not find it beside itself (/usr/bin, Program Files)
    if let Ok(dir) = app.path().resource_dir() {
        let catalog = dir.join("catalog");
        if catalog.join("library.json").exists() {
            command = command.env("EMBSIM_CATALOG", catalog);
        }
    }
    let (_events, child) = command.spawn().map_err(|e| format!("cannot start embsim: {e}"))?;
    let url = format!("ws://127.0.0.1:{port}/ws");
    // the engine loads the project before it listens: wait until it does
    // (at most five seconds) so the workbench's first connection succeeds
    let addr = std::net::SocketAddr::from(([127, 0, 0, 1], port));
    let deadline = std::time::Instant::now() + std::time::Duration::from_secs(5);
    while std::net::TcpStream::connect_timeout(&addr, std::time::Duration::from_millis(100)).is_err() {
        if std::time::Instant::now() > deadline {
            let _ = child.kill();
            return Err(format!("embsim did not start listening on port {port}"));
        }
        std::thread::sleep(std::time::Duration::from_millis(50));
    }
    *state.child.lock().map_err(|e| e.to_string())? = Some(child);
    *state.url.lock().map_err(|e| e.to_string())? = Some(url.clone());
    Ok(SidecarInfo { ws_url: url })
}

/// The workbench's errors (console.error, uncaught exceptions), to stderr:
/// the webview's console is otherwise out of sight.
#[tauri::command]
fn log_message(level: String, message: String) {
    eprintln!("workbench {level}: {message}");
}

/// Menu state from the workbench (running, paused); reserved for native menus.
#[tauri::command]
fn set_menu_state(_state: serde_json::Value) {}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_fs::init())
        .manage(Sidecar::default())
        .invoke_handler(tauri::generate_handler![start_sidecar, set_menu_state, log_message])
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::Destroyed = event {
                let state = window.app_handle().state::<Sidecar>();
                let child = state.child.lock().ok().and_then(|mut c| c.take());
                if let Some(child) = child {
                    let _ = child.kill();
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("EmbSIM desktop shell failed to start");
}
