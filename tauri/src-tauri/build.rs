//! Build script for vivid-tauri
//!
//! Copies libvivid-c to the target directory and sets up rpath

use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    tauri_build::build();

    // Get paths
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let out_dir = env::var("OUT_DIR").unwrap();
    let profile = env::var("PROFILE").unwrap(); // "debug" or "release"

    // Vivid root is tauri/src-tauri -> tauri -> vivid
    let vivid_root = PathBuf::from(&manifest_dir)
        .parent() // tauri
        .unwrap()
        .parent() // vivid
        .unwrap()
        .to_path_buf();

    let lib_dir = vivid_root.join("build").join("lib");

    // Target directory: go up from OUT_DIR to find target/<profile>
    // OUT_DIR is something like target/debug/build/vivid-tauri-xxx/out
    let target_dir = PathBuf::from(&out_dir)
        .ancestors()
        .find(|p| p.ends_with(&profile))
        .map(|p| p.to_path_buf())
        .unwrap_or_else(|| {
            // Fallback: construct from manifest dir
            PathBuf::from(&manifest_dir)
                .parent()
                .unwrap()
                .join("target")
                .join(&profile)
        });

    // Copy the dylibs to target directory
    #[cfg(target_os = "macos")]
    {
        // Libraries to copy
        let libs = [
            "libvivid-c.dylib",
            "libvivid-c.1.dylib",
            "libvivid-core.dylib",
        ];

        for lib_name in &libs {
            let src = lib_dir.join(lib_name);
            let dst = target_dir.join(lib_name);

            if src.exists() {
                // Read the actual file (following symlinks)
                if let Ok(content) = fs::read(&src) {
                    let _ = fs::write(&dst, &content);
                    println!("cargo:warning=Copied {} to {}", lib_name, dst.display());
                }
            }
        }

        let src_lib = lib_dir.join("libvivid-c.dylib");
        if src_lib.exists() {
            // Set rpath to look in executable directory
            println!("cargo:rustc-link-arg=-Wl,-rpath,@executable_path");
            println!("cargo:rustc-link-arg=-Wl,-rpath,@loader_path");
        } else {
            println!("cargo:warning=libvivid-c.dylib not found at {}", src_lib.display());
            println!("cargo:warning=Run 'cmake --build build --target vivid-c' first");
        }
    }

    #[cfg(target_os = "linux")]
    {
        let src_lib = lib_dir.join("libvivid-c.so");
        let dst_lib = target_dir.join("libvivid-c.so");

        if src_lib.exists() {
            if let Ok(content) = fs::read(&src_lib) {
                let _ = fs::write(&dst_lib, &content);
                println!("cargo:warning=Copied libvivid-c.so to {}", dst_lib.display());
            }

            println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN");
        }
    }

    #[cfg(target_os = "windows")]
    {
        let src_lib = lib_dir.join("vivid-c.dll");
        let dst_lib = target_dir.join("vivid-c.dll");

        if src_lib.exists() {
            let _ = fs::copy(&src_lib, &dst_lib);
            println!("cargo:warning=Copied vivid-c.dll to {}", dst_lib.display());
        }
    }

    // Copy shaders directory
    let shaders_src = vivid_root.join("build").join("shaders");
    let shaders_dst = target_dir.join("shaders");
    if shaders_src.exists() {
        let _ = fs::create_dir_all(&shaders_dst);
        if let Ok(entries) = fs::read_dir(&shaders_src) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_file() {
                    if let Some(filename) = path.file_name() {
                        let dst = shaders_dst.join(filename);
                        let _ = fs::copy(&path, &dst);
                    }
                }
            }
        }
        println!("cargo:warning=Copied shaders to {}", shaders_dst.display());
    }

    // Copy fonts directory
    let fonts_src = vivid_root.join("build").join("fonts");
    let fonts_dst = target_dir.join("fonts");
    if fonts_src.exists() {
        let _ = fs::create_dir_all(&fonts_dst);
        if let Ok(entries) = fs::read_dir(&fonts_src) {
            for entry in entries.flatten() {
                let path = entry.path();
                if path.is_file() {
                    if let Some(filename) = path.file_name() {
                        let dst = fonts_dst.join(filename);
                        let _ = fs::copy(&path, &dst);
                    }
                }
            }
        }
        println!("cargo:warning=Copied fonts to {}", fonts_dst.display());
    }

    // Rerun if library changes
    println!("cargo:rerun-if-changed={}", lib_dir.join("libvivid-c.dylib").display());
    println!("cargo:rerun-if-changed={}", lib_dir.join("libvivid-core.dylib").display());
    println!("cargo:rerun-if-changed={}", lib_dir.join("libvivid-c.so").display());
    println!("cargo:rerun-if-changed={}", lib_dir.join("libvivid-core.so").display());
    println!("cargo:rerun-if-changed={}", lib_dir.join("vivid-c.dll").display());
    println!("cargo:rerun-if-changed={}", lib_dir.join("vivid-core.dll").display());
    println!("cargo:rerun-if-changed={}", shaders_src.display());
    println!("cargo:rerun-if-changed={}", fonts_src.display());
}
