// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod file_ops;
mod pty;

use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::sync::OnceLock;
use std::time::Instant;
use tauri::{Manager, RunEvent, WindowEvent, Emitter};
use tauri::menu::{Menu, MenuBuilder, MenuItemBuilder, SubmenuBuilder, PredefinedMenuItem};
use serde::{Deserialize, Serialize};

// Vivid context wrapper
struct VividState {
    ctx: vivid::Context,
}

// Need unsafe impl Send because vivid::Context contains raw pointers
// but vivid is single-threaded and we only access from main thread
unsafe impl Send for VividState {}
unsafe impl Sync for VividState {}

// =============================================================================
// Serializable types for webview communication
// =============================================================================

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ProjectInfo {
    pub loaded: bool,
    pub project_path: Option<String>,
    pub chain_path: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompileStatusInfo {
    pub success: bool,
    pub message: Option<String>,
    pub error_line: Option<u32>,
    pub error_column: Option<u32>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct OperatorInfo {
    pub name: String,
    pub type_name: String,
    pub output_kind: String,
    pub bypassed: bool,
    pub input_count: usize,
    pub inputs: Vec<String>,  // Names of input operators
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ParamInfo {
    pub name: String,
    pub param_type: String,
    pub min_val: f32,
    pub max_val: f32,
    pub value: [f32; 4],
    pub default_val: [f32; 4],
    pub enum_labels: Vec<String>,
}

// =============================================================================
// Tauri commands for vivid state
// =============================================================================

#[tauri::command]
fn get_project_info() -> ProjectInfo {
    log::info!("[Tauri] get_project_info called");
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(state) = state.lock() {
            let project_path = state.ctx.project_path();
            let chain_path = project_path.as_ref().map(|p| format!("{}/chain.cpp", p));
            let info = ProjectInfo {
                loaded: state.ctx.has_project(),
                project_path: project_path.clone(),
                chain_path,
            };
            log::info!("[Tauri] get_project_info returning: loaded={}, path={:?}", info.loaded, project_path);
            return info;
        }
    }
    log::info!("[Tauri] get_project_info: VIVID_STATE not ready");
    ProjectInfo {
        loaded: false,
        project_path: None,
        chain_path: None,
    }
}

#[tauri::command]
fn get_compile_status() -> CompileStatusInfo {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(state) = state.lock() {
            let status = state.ctx.compile_status();
            return CompileStatusInfo {
                success: status.success,
                message: status.message,
                error_line: status.error_line,
                error_column: status.error_column,
            };
        }
    }
    CompileStatusInfo {
        success: true,
        message: None,
        error_line: None,
        error_column: None,
    }
}

#[tauri::command]
fn get_operators() -> Vec<OperatorInfo> {
    log::info!("[Tauri] get_operators called");
    let mut operators = Vec::new();

    if let Some(state) = VIVID_STATE.get() {
        if let Ok(state) = state.lock() {
            if let Some(chain) = state.ctx.chain() {
                for op in chain.operators() {
                    let mut inputs = Vec::new();
                    for i in 0..op.input_count() {
                        inputs.push(op.input_name(i));
                    }

                    operators.push(OperatorInfo {
                        name: op.name(),
                        type_name: op.type_name(),
                        output_kind: format!("{:?}", op.output_kind()),
                        bypassed: op.is_bypassed(),
                        input_count: op.input_count(),
                        inputs,
                    });
                }
            }
        }
    }

    log::info!("[Tauri] get_operators returning {} operators", operators.len());
    operators
}

#[tauri::command]
fn get_operator_params(op_name: String) -> Vec<ParamInfo> {
    let mut params = Vec::new();

    if let Some(state) = VIVID_STATE.get() {
        if let Ok(state) = state.lock() {
            if let Some(chain) = state.ctx.chain() {
                if let Some(op) = chain.operator_by_name(&op_name) {
                    for decl in op.params() {
                        let value = op.get_param(&decl.name).unwrap_or([0.0; 4]);
                        params.push(ParamInfo {
                            name: decl.name,
                            param_type: format!("{:?}", decl.param_type),
                            min_val: decl.min_val,
                            max_val: decl.max_val,
                            value,
                            default_val: decl.default_val,
                            enum_labels: decl.enum_labels,
                        });
                    }
                }
            }
        }
    }

    params
}

#[tauri::command]
fn set_param(op_name: String, param_name: String, value: [f32; 4]) -> bool {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(state) = state.lock() {
            if let Some(chain) = state.ctx.chain() {
                if let Some(mut op) = chain.operator_by_name(&op_name) {
                    return op.set_param(&param_name, &value);
                }
            }
        }
    }
    false
}

#[tauri::command]
fn reload_project() -> Result<(), String> {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(mut state) = state.lock() {
            state.ctx.reload().map_err(|e| e.to_string())
        } else {
            Err("Failed to lock state".into())
        }
    } else {
        Err("Vivid not initialized".into())
    }
}

// Input event commands - forward from webview to vivid
#[tauri::command]
fn input_mouse_move(x: f32, y: f32) {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(mut state) = state.lock() {
            state.ctx.set_mouse_position(x, y);
        }
    }
}

#[tauri::command]
fn input_mouse_button(button: u32, pressed: bool) {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(mut state) = state.lock() {
            state.ctx.set_mouse_button(button, pressed);
        }
    }
}

#[tauri::command]
fn input_scroll(dx: f32, dy: f32) {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(mut state) = state.lock() {
            state.ctx.add_scroll(dx, dy);
        }
    }
}

#[tauri::command]
fn load_project(path: String) -> Result<(), String> {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(mut state) = state.lock() {
            state.ctx.load_project(&path).map_err(|e| e.to_string())
        } else {
            Err("Failed to lock state".into())
        }
    } else {
        Err("Vivid not initialized".into())
    }
}

#[tauri::command]
fn toggle_visualizer() {
    log::info!("[Tauri] toggle_visualizer called");
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(mut state) = state.lock() {
            let visible = state.ctx.is_visualizer_visible();
            log::info!("[Tauri] toggle_visualizer: was {}, setting to {}", visible, !visible);
            state.ctx.set_visualizer_visible(!visible);
        }
    } else {
        log::info!("[Tauri] toggle_visualizer: VIVID_STATE not ready");
    }
}

#[tauri::command]
fn get_selected_operator() -> Option<String> {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(state) = state.lock() {
            return state.ctx.selected_operator();
        }
    }
    None
}

#[tauri::command]
fn select_operator(name: String) {
    if let Some(state) = VIVID_STATE.get() {
        if let Ok(mut state) = state.lock() {
            state.ctx.select_operator(&name);
        }
    }
}

/// Global vivid state, initialized after window is ready
static VIVID_STATE: OnceLock<Mutex<VividState>> = OnceLock::new();
/// Frame counter to delay initialization
static FRAME_COUNT: AtomicU64 = AtomicU64::new(0);
/// Whether we've tried to initialize
static INIT_ATTEMPTED: AtomicBool = AtomicBool::new(false);
/// Start time for render loop
static START_TIME: OnceLock<Instant> = OnceLock::new();

/// Get the NSWindow pointer from a Tauri window on macOS
#[cfg(target_os = "macos")]
fn get_ns_window(window: &tauri::WebviewWindow) -> Option<*mut std::ffi::c_void> {
    // Get the tao window
    let tao_window = window.as_ref().window();

    // On macOS, we can use raw-window-handle to get the NSWindow
    use raw_window_handle::{HasWindowHandle, RawWindowHandle};

    if let Ok(handle) = tao_window.window_handle() {
        match handle.as_raw() {
            RawWindowHandle::AppKit(appkit_handle) => {
                // ns_view is a NonNull<c_void>, convert to NSWindow
                // The ns_view is actually the contentView of the NSWindow
                // We need to get the window from it
                let ns_view = appkit_handle.ns_view.as_ptr();

                // Use Objective-C to get the window from the view
                unsafe {
                    use objc2::msg_send;
                    use objc2::runtime::AnyObject;

                    let view: *mut AnyObject = ns_view as *mut _;
                    let window: *mut AnyObject = msg_send![view, window];

                    if window.is_null() {
                        None
                    } else {
                        Some(window as *mut std::ffi::c_void)
                    }
                }
            }
            _ => None,
        }
    } else {
        None
    }
}

#[cfg(not(target_os = "macos"))]
fn get_ns_window(_window: &tauri::WebviewWindow) -> Option<*mut std::ffi::c_void> {
    // TODO: Implement for Windows/Linux
    None
}

/// Create the native application menu
fn create_app_menu(app: &tauri::App) -> Result<Menu<tauri::Wry>, Box<dyn std::error::Error>> {
    // App menu (macOS only, but we define it anyway)
    let app_menu = SubmenuBuilder::new(app, "Vivid")
        .item(&PredefinedMenuItem::about(app, Some("About Vivid"), None)?)
        .separator()
        .item(&PredefinedMenuItem::services(app, None)?)
        .separator()
        .item(&PredefinedMenuItem::hide(app, Some("Hide Vivid"))?)
        .item(&PredefinedMenuItem::hide_others(app, Some("Hide Others"))?)
        .item(&PredefinedMenuItem::show_all(app, Some("Show All"))?)
        .separator()
        .item(&PredefinedMenuItem::quit(app, Some("Quit Vivid"))?)
        .build()?;

    // File menu
    let file_menu = SubmenuBuilder::new(app, "File")
        .item(&MenuItemBuilder::with_id("new_project", "New Project...")
            .accelerator("CmdOrCtrl+N")
            .build(app)?)
        .item(&MenuItemBuilder::with_id("open_project", "Open Project...")
            .accelerator("CmdOrCtrl+O")
            .build(app)?)
        .separator()
        .item(&MenuItemBuilder::with_id("open_file", "Open File...")
            .accelerator("CmdOrCtrl+Shift+O")
            .build(app)?)
        .item(&MenuItemBuilder::with_id("save", "Save")
            .accelerator("CmdOrCtrl+S")
            .build(app)?)
        .separator()
        .item(&MenuItemBuilder::with_id("reload", "Reload Project")
            .accelerator("CmdOrCtrl+R")
            .build(app)?)
        .build()?;

    // Edit menu
    let edit_menu = SubmenuBuilder::new(app, "Edit")
        .item(&PredefinedMenuItem::undo(app, None)?)
        .item(&PredefinedMenuItem::redo(app, None)?)
        .separator()
        .item(&PredefinedMenuItem::cut(app, None)?)
        .item(&PredefinedMenuItem::copy(app, None)?)
        .item(&PredefinedMenuItem::paste(app, None)?)
        .item(&PredefinedMenuItem::select_all(app, None)?)
        .build()?;

    // View menu
    let view_menu = SubmenuBuilder::new(app, "View")
        .item(&MenuItemBuilder::with_id("toggle_terminal", "Toggle Terminal")
            .accelerator("CmdOrCtrl+1")
            .build(app)?)
        .item(&MenuItemBuilder::with_id("toggle_inspector", "Toggle Inspector")
            .accelerator("CmdOrCtrl+2")
            .build(app)?)
        .item(&MenuItemBuilder::with_id("toggle_editor", "Toggle Editor")
            .accelerator("CmdOrCtrl+3")
            .build(app)?)
        .separator()
        .item(&MenuItemBuilder::with_id("toggle_visualizer", "Toggle Node Graph")
            .accelerator("Tab")
            .build(app)?)
        .separator()
        .item(&PredefinedMenuItem::fullscreen(app, None)?)
        .build()?;

    // Window menu
    let window_menu = SubmenuBuilder::new(app, "Window")
        .item(&PredefinedMenuItem::minimize(app, None)?)
        .item(&PredefinedMenuItem::maximize(app, None)?)
        .separator()
        .item(&PredefinedMenuItem::close_window(app, Some("Close"))?)
        .build()?;

    // Help menu
    let help_menu = SubmenuBuilder::new(app, "Help")
        .item(&MenuItemBuilder::with_id("docs", "Vivid Documentation")
            .build(app)?)
        .build()?;

    // Build the full menu
    let menu = MenuBuilder::new(app)
        .item(&app_menu)
        .item(&file_menu)
        .item(&edit_menu)
        .item(&view_menu)
        .item(&window_menu)
        .item(&help_menu)
        .build()?;

    Ok(menu)
}

fn main() {
    env_logger::init();

    // Create PTY manager
    let pty_manager = Arc::new(pty::PtyManager::new());

    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_dialog::init())
        .manage(pty_manager)
        .setup(|app| {
            log::info!("Vivid Tauri app setup starting...");

            // Build the application menu
            let menu = create_app_menu(app)?;
            app.set_menu(menu)?;

            log::info!("Vivid Tauri app setup complete");
            Ok(())
        })
        .on_menu_event(|app, event| {
            log::info!("Menu event: {:?}", event.id());
            let window = app.get_webview_window("main");

            match event.id().0.as_str() {
                "new_project" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "new_project");
                    }
                }
                "open_project" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "open_project");
                    }
                }
                "open_file" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "open_file");
                    }
                }
                "save" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "save");
                    }
                }
                "reload" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "reload");
                    }
                }
                "toggle_terminal" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "toggle_terminal");
                    }
                }
                "toggle_inspector" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "toggle_inspector");
                    }
                }
                "toggle_editor" => {
                    if let Some(win) = window {
                        let _ = win.emit("menu-action", "toggle_editor");
                    }
                }
                "toggle_visualizer" => {
                    if let Some(state) = VIVID_STATE.get() {
                        if let Ok(mut state) = state.lock() {
                            let visible = state.ctx.is_visualizer_visible();
                            state.ctx.set_visualizer_visible(!visible);
                        }
                    }
                }
                _ => {}
            }
        })
        .invoke_handler(tauri::generate_handler![
            // PTY commands
            pty::spawn_shell,
            pty::write_pty,
            pty::resize_pty,
            pty::close_pty,
            // File operations
            file_ops::read_file,
            file_ops::write_file,
            file_ops::get_file_name,
            file_ops::create_project,
            // Vivid state queries
            get_project_info,
            get_compile_status,
            get_operators,
            get_operator_params,
            set_param,
            reload_project,
            // Input forwarding
            input_mouse_move,
            input_mouse_button,
            input_scroll,
            load_project,
            toggle_visualizer,
            get_selected_operator,
            select_operator,
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

                    // Wait ~30 frames (about 500ms at 60fps) before trying to init vivid
                    // This ensures the Metal layer is ready on macOS
                    if frame == 30 && !INIT_ATTEMPTED.swap(true, Ordering::SeqCst) {
                        log::info!("Initializing vivid on main thread");

                        if let Some(window) = app_handle.get_webview_window("main") {
                            if let Some(ns_window) = get_ns_window(&window) {
                                // Get window size
                                let size = window.inner_size().unwrap_or_default();
                                let config = vivid::ContextConfig::new(
                                    size.width.max(1),
                                    size.height.max(1),
                                );

                                // Create vivid context with window
                                match unsafe { vivid::Context::with_window(ns_window, config) } {
                                    Ok(mut ctx) => {
                                        // Auto-load a test project for development
                                        let test_project = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                                            .parent().unwrap()  // tauri
                                            .parent().unwrap()  // vivid
                                            .join("projects/getting-started/02-operator-pipeline");

                                        if test_project.exists() {
                                            match ctx.load_project(&test_project) {
                                                Ok(_) => log::info!("Loaded test project: {:?}", test_project),
                                                Err(e) => log::warn!("Failed to load test project: {:?}", e),
                                            }
                                        }

                                        let state = VividState { ctx };
                                        if VIVID_STATE.set(Mutex::new(state)).is_ok() {
                                            log::info!("Vivid initialized successfully on main thread!");
                                        }
                                    }
                                    Err(e) => {
                                        log::error!("Failed to create vivid context: {:?}", e);
                                    }
                                }
                            } else {
                                log::error!("Failed to get NSWindow handle");
                            }
                        }
                    }

                    // Render if vivid is initialized
                    if let Some(state) = VIVID_STATE.get() {
                        if let Ok(state) = state.lock() {
                            if let Err(e) = state.ctx.render_frame() {
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
                        if let Some(state) = VIVID_STATE.get() {
                            if let Ok(mut state) = state.lock() {
                                if let Err(e) = state.ctx.resize_surface(size.width, size.height) {
                                    log::error!("Resize error: {:?}", e);
                                }
                            }
                        }
                    }
                }
                _ => {}
            }
        });
}
