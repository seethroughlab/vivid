//! Build script for vivid-sys
//!
//! Configures the linker to find libvivid-c

use std::env;
use std::path::PathBuf;

fn main() {
    // Get the vivid root directory (tauri/crates/vivid-sys -> tauri -> vivid)
    let manifest_dir = env::var("CARGO_MANIFEST_DIR").unwrap();
    let vivid_root = PathBuf::from(&manifest_dir)
        .parent() // crates
        .unwrap()
        .parent() // tauri
        .unwrap()
        .parent() // vivid
        .unwrap()
        .to_path_buf();

    // Library is in vivid/build/lib/
    let lib_dir = vivid_root.join("build").join("lib");

    // Tell cargo where to find the library
    println!("cargo:rustc-link-search=native={}", lib_dir.display());

    // Link against vivid-c
    println!("cargo:rustc-link-lib=dylib=vivid-c");

    // On macOS, we need to set the rpath so the dylib can be found at runtime
    #[cfg(target_os = "macos")]
    {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", lib_dir.display());
        println!("cargo:rustc-link-arg=-Wl,-rpath,@executable_path/../lib");
    }

    // On Linux, set rpath
    #[cfg(target_os = "linux")]
    {
        println!("cargo:rustc-link-arg=-Wl,-rpath,{}", lib_dir.display());
        println!("cargo:rustc-link-arg=-Wl,-rpath,$ORIGIN/../lib");
    }

    // Rerun if the library changes
    println!("cargo:rerun-if-changed={}/libvivid-c.dylib", lib_dir.display());
    println!("cargo:rerun-if-changed={}/libvivid-c.so", lib_dir.display());
    println!("cargo:rerun-if-changed={}/vivid-c.dll", lib_dir.display());
}
