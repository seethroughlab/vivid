use parking_lot::Mutex;
use portable_pty::{native_pty_system, CommandBuilder, PtyPair, PtySize};
use std::collections::HashMap;
use std::io::{Read, Write};
use std::sync::Arc;
use std::thread;
use tauri::{AppHandle, Emitter};

/// Manages PTY sessions for the terminal
pub struct PtyManager {
    sessions: Mutex<HashMap<u32, PtySession>>,
    next_id: Mutex<u32>,
}

struct PtySession {
    pair: PtyPair,
    writer: Box<dyn Write + Send>,
}

impl PtyManager {
    pub fn new() -> Self {
        Self {
            sessions: Mutex::new(HashMap::new()),
            next_id: Mutex::new(1),
        }
    }

    /// Spawn a new shell session and return its ID
    pub fn spawn_shell(&self, app_handle: AppHandle, rows: u16, cols: u16) -> Result<u32, String> {
        let pty_system = native_pty_system();

        let pair = pty_system
            .openpty(PtySize {
                rows,
                cols,
                pixel_width: 0,
                pixel_height: 0,
            })
            .map_err(|e| format!("Failed to open PTY: {}", e))?;

        // Get the user's default shell
        let shell = std::env::var("SHELL").unwrap_or_else(|_| "/bin/zsh".to_string());

        let mut cmd = CommandBuilder::new(&shell);
        cmd.env("TERM", "xterm-256color");
        cmd.env("COLORTERM", "truecolor");

        // Spawn the shell in the PTY
        let _child = pair
            .slave
            .spawn_command(cmd)
            .map_err(|e| format!("Failed to spawn shell: {}", e))?;

        // Get writer for sending input to PTY
        let writer = pair
            .master
            .take_writer()
            .map_err(|e| format!("Failed to get PTY writer: {}", e))?;

        // Get reader for receiving output from PTY
        let mut reader = pair
            .master
            .try_clone_reader()
            .map_err(|e| format!("Failed to get PTY reader: {}", e))?;

        // Generate session ID
        let session_id = {
            let mut id = self.next_id.lock();
            let current = *id;
            *id += 1;
            current
        };

        // Store the session
        {
            let mut sessions = self.sessions.lock();
            sessions.insert(
                session_id,
                PtySession {
                    pair,
                    writer,
                },
            );
        }

        // Spawn a thread to read PTY output and emit to frontend
        let app = app_handle.clone();
        let sid = session_id;
        thread::spawn(move || {
            let mut buf = [0u8; 4096];
            loop {
                match reader.read(&mut buf) {
                    Ok(0) => {
                        // EOF - shell exited
                        let _ = app.emit("pty-exit", sid);
                        break;
                    }
                    Ok(n) => {
                        // Send output to frontend
                        let data = String::from_utf8_lossy(&buf[..n]).to_string();
                        let _ = app.emit("pty-output", (sid, data));
                    }
                    Err(e) => {
                        log::error!("PTY read error: {}", e);
                        break;
                    }
                }
            }
        });

        log::info!("Spawned shell session {} with shell: {}", session_id, shell);
        Ok(session_id)
    }

    /// Write data to a PTY session
    pub fn write(&self, session_id: u32, data: &str) -> Result<(), String> {
        let mut sessions = self.sessions.lock();
        let session = sessions
            .get_mut(&session_id)
            .ok_or_else(|| format!("Session {} not found", session_id))?;

        session
            .writer
            .write_all(data.as_bytes())
            .map_err(|e| format!("Write error: {}", e))?;

        session
            .writer
            .flush()
            .map_err(|e| format!("Flush error: {}", e))?;

        Ok(())
    }

    /// Resize a PTY session
    pub fn resize(&self, session_id: u32, rows: u16, cols: u16) -> Result<(), String> {
        let sessions = self.sessions.lock();
        let session = sessions
            .get(&session_id)
            .ok_or_else(|| format!("Session {} not found", session_id))?;

        session
            .pair
            .master
            .resize(PtySize {
                rows,
                cols,
                pixel_width: 0,
                pixel_height: 0,
            })
            .map_err(|e| format!("Resize error: {}", e))?;

        Ok(())
    }

    /// Close a PTY session
    pub fn close(&self, session_id: u32) -> Result<(), String> {
        let mut sessions = self.sessions.lock();
        sessions
            .remove(&session_id)
            .ok_or_else(|| format!("Session {} not found", session_id))?;
        log::info!("Closed shell session {}", session_id);
        Ok(())
    }
}

// Tauri commands

#[tauri::command]
pub fn spawn_shell(
    app_handle: AppHandle,
    state: tauri::State<'_, Arc<PtyManager>>,
    rows: u16,
    cols: u16,
) -> Result<u32, String> {
    state.spawn_shell(app_handle, rows, cols)
}

#[tauri::command]
pub fn write_pty(
    state: tauri::State<'_, Arc<PtyManager>>,
    session_id: u32,
    data: String,
) -> Result<(), String> {
    state.write(session_id, &data)
}

#[tauri::command]
pub fn resize_pty(
    state: tauri::State<'_, Arc<PtyManager>>,
    session_id: u32,
    rows: u16,
    cols: u16,
) -> Result<(), String> {
    state.resize(session_id, rows, cols)
}

#[tauri::command]
pub fn close_pty(
    state: tauri::State<'_, Arc<PtyManager>>,
    session_id: u32,
) -> Result<(), String> {
    state.close(session_id)
}
