use std::fs;
use std::path::PathBuf;

#[tauri::command]
pub async fn read_file(path: String) -> Result<String, String> {
    fs::read_to_string(&path).map_err(|e| format!("Failed to read file: {}", e))
}

#[tauri::command]
pub async fn write_file(path: String, content: String) -> Result<(), String> {
    fs::write(&path, &content).map_err(|e| format!("Failed to write file: {}", e))
}

#[tauri::command]
pub fn get_file_name(path: String) -> String {
    PathBuf::from(&path)
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| path.clone())
}

#[tauri::command]
pub async fn create_project(path: String, name: String, template: Option<String>) -> Result<(), String> {
    use std::process::Command;

    let parent_path = PathBuf::from(&path);

    // Get the parent directory where we'll run `vivid new`
    let parent_dir = parent_path.parent()
        .ok_or_else(|| "Invalid project path".to_string())?;

    // Ensure parent directory exists
    if !parent_dir.exists() {
        fs::create_dir_all(parent_dir)
            .map_err(|e| format!("Failed to create parent directory: {}", e))?;
    }

    // Find vivid executable
    // In dev: it's in the same directory as the Tauri app (build/bin/)
    // We need to go from tauri/src-tauri/target/debug to build/bin/vivid
    let exe_dir = std::env::current_exe()
        .map_err(|e| format!("Could not determine executable path: {}", e))?
        .parent()
        .map(|p| p.to_path_buf())
        .ok_or_else(|| "Could not determine executable directory".to_string())?;

    // Try multiple possible locations for vivid executable
    let possible_paths = [
        exe_dir.join("vivid"),                                    // Same dir as Tauri app
        exe_dir.join("../../../build/bin/vivid"),                 // Dev: tauri/src-tauri/target/debug -> build/bin
        exe_dir.join("../../../../build/bin/vivid"),              // Dev: tauri/src-tauri/target/release -> build/bin
        PathBuf::from("/usr/local/bin/vivid"),                    // System install
    ];

    let vivid_exe = possible_paths.iter()
        .find(|p| p.exists())
        .ok_or_else(|| "Could not find vivid executable".to_string())?;

    // Build command: vivid new <name> -y -t <template>
    let template_name = template.unwrap_or_else(|| "blank".to_string());

    let output = Command::new(vivid_exe)
        .current_dir(parent_dir)
        .args(["new", &name, "-y", "-t", &template_name])
        .output()
        .map_err(|e| format!("Failed to execute vivid new: {}", e))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        let stdout = String::from_utf8_lossy(&output.stdout);
        return Err(format!("vivid new failed: {}{}", stdout, stderr));
    }

    Ok(())
}
