// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod egui_state;
mod file_ops;
mod node_graph;
mod pty;
mod wgpu_state;

use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;
use std::sync::OnceLock;
use std::time::Instant;
use tauri::{Manager, RunEvent, WindowEvent};
use wgpu_state::WgpuState;

// Input event commands - forward from webview to egui

#[tauri::command]
fn input_mouse_move(x: f32, y: f32) {
    if let Some(wgpu_state) = WGPU_STATE.get() {
        wgpu_state.inject_mouse_pos(x, y);
    }
}

#[tauri::command]
fn input_mouse_button(button: u32, pressed: bool) {
    if let Some(wgpu_state) = WGPU_STATE.get() {
        wgpu_state.inject_mouse_button(button, pressed);
    }
}

#[tauri::command]
fn input_scroll(dx: f32, dy: f32) {
    if let Some(wgpu_state) = WGPU_STATE.get() {
        wgpu_state.inject_scroll(dx, dy);
    }
}

/// Global wgpu state, initialized after window is ready
static WGPU_STATE: OnceLock<Arc<WgpuState>> = OnceLock::new();
/// Frame counter to delay initialization
static FRAME_COUNT: AtomicU64 = AtomicU64::new(0);
/// Whether we've tried to initialize
static INIT_ATTEMPTED: AtomicBool = AtomicBool::new(false);
/// Start time for render loop
static START_TIME: OnceLock<Instant> = OnceLock::new();

fn main() {
    env_logger::init();

    // Create PTY manager
    let pty_manager = Arc::new(pty::PtyManager::new());

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(pty_manager)
        .setup(|_app| {
            log::info!("Vivid Tauri app setup complete");
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            pty::spawn_shell,
            pty::write_pty,
            pty::resize_pty,
            pty::close_pty,
            file_ops::read_file,
            file_ops::write_file,
            file_ops::get_file_name,
            input_mouse_move,
            input_mouse_button,
            input_scroll,
        ])
        .build(tauri::generate_context!())
        .expect("error while building tauri application")
        .run(|app_handle, event| {
            match event {
                RunEvent::Ready => {
                    log::info!("RunEvent::Ready");
                    let _ = START_TIME.set(Instant::now());
                }
                // MainEventsCleared fires after each batch of events - good for periodic work
                RunEvent::MainEventsCleared => {
                    let frame = FRAME_COUNT.fetch_add(1, Ordering::SeqCst);

                    // Wait ~30 frames (about 500ms at 60fps) before trying to init wgpu
                    // This ensures the Metal layer is ready on macOS
                    if frame == 30 && !INIT_ATTEMPTED.swap(true, Ordering::SeqCst) {
                        log::info!("Initializing wgpu on main thread");

                        if let Some(window) = app_handle.get_webview_window("main") {
                            // Create wgpu state on main thread (required for Metal)
                            let wgpu_state = pollster::block_on(WgpuState::new(&window));
                            let wgpu_state = Arc::new(wgpu_state);

                            if WGPU_STATE.set(wgpu_state.clone()).is_ok() {
                                log::info!("wgpu initialized successfully on main thread!");
                            }
                        }
                    }

                    // Render if wgpu is initialized
                    if let Some(wgpu_state) = WGPU_STATE.get() {
                        if let Some(start) = START_TIME.get() {
                            let time = start.elapsed().as_secs_f32();
                            if let Err(e) = wgpu_state.render(time) {
                                log::error!("Render error: {:?}", e);
                            }
                        }
                    }
                }
                RunEvent::WindowEvent {
                    label: _,
                    event: WindowEvent::Resized(size),
                    ..
                } => {
                    if size.width > 0 && size.height > 0 {
                        if let Some(wgpu_state) = WGPU_STATE.get() {
                            // Get scale factor from window
                            let scale_factor = app_handle
                                .get_webview_window("main")
                                .and_then(|w| w.scale_factor().ok())
                                .map(|s| s as f32);
                            wgpu_state.resize(size.width, size.height, scale_factor);
                        }
                    }
                }
                _ => {}
            }
        });
}
