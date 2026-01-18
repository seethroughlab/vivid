#!/bin/bash
# create-vivid-ide-repo.sh
# Creates the vivid-ide repository structure with vivid as a git submodule
#
# Usage:
#   ./create-vivid-ide-repo.sh /path/to/create/vivid-ide
#
# This script:
#   1. Creates the vivid-ide directory structure
#   2. Copies the tauri/ contents from vivid
#   3. Sets up vivid as a git submodule
#   4. Creates the necessary build configuration
#   5. Initializes git repository

set -e

VIVID_REPO="${VIVID_REPO:-https://github.com/seethroughlab/vivid.git}"
VIVID_BRANCH="${VIVID_BRANCH:-master}"

# Check arguments
if [ -z "$1" ]; then
    echo "Usage: $0 <target-directory>"
    echo "Example: $0 ~/Developer/vivid-ide"
    exit 1
fi

TARGET_DIR="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VIVID_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "Creating vivid-ide repository at: $TARGET_DIR"
echo "Source vivid directory: $VIVID_DIR"
echo ""

# Check if target exists
if [ -d "$TARGET_DIR" ]; then
    echo "Error: Target directory already exists: $TARGET_DIR"
    exit 1
fi

# Create directory structure
mkdir -p "$TARGET_DIR"
cd "$TARGET_DIR"

# Initialize git repo
git init

# Add vivid as submodule
echo "Adding vivid as git submodule..."
git submodule add -b "$VIVID_BRANCH" "$VIVID_REPO" vivid

# Copy tauri contents (excluding node_modules, target, dist)
echo "Copying Tauri IDE files..."
mkdir -p src-tauri/src
mkdir -p src-tauri/capabilities
mkdir -p src-tauri/icons
mkdir -p crates/vivid-sys
mkdir -p crates/vivid
mkdir -p src

# Copy source files
cp -r "$VIVID_DIR/tauri/src-tauri/src/"* src-tauri/src/
cp -r "$VIVID_DIR/tauri/src-tauri/capabilities/"* src-tauri/capabilities/
cp -r "$VIVID_DIR/tauri/src-tauri/icons/"* src-tauri/icons/
cp "$VIVID_DIR/tauri/src-tauri/Cargo.toml" src-tauri/
cp "$VIVID_DIR/tauri/src-tauri/tauri.conf.json" src-tauri/
cp -r "$VIVID_DIR/tauri/crates/vivid-sys/"* crates/vivid-sys/
cp -r "$VIVID_DIR/tauri/crates/vivid/"* crates/vivid/
cp -r "$VIVID_DIR/tauri/src/"* src/
cp "$VIVID_DIR/tauri/index.html" .
cp "$VIVID_DIR/tauri/package.json" .
cp "$VIVID_DIR/tauri/tsconfig.json" .
cp "$VIVID_DIR/tauri/vite.config.ts" .

# Create updated Cargo.toml for workspace
cat > Cargo.toml << 'EOF'
[workspace]
members = ["src-tauri", "crates/vivid-sys", "crates/vivid"]
resolver = "2"
EOF

# Update src-tauri/Cargo.toml to reference submodule paths
# The vivid-sys crate needs to find headers in vivid/modules/vivid-core/include
cat > crates/vivid-sys/build.rs << 'EOF'
use std::path::Path;

fn main() {
    // Priority 1: CI provides pre-built library via environment variable
    if let Ok(lib_path) = std::env::var("VIVID_LIB_PATH") {
        println!("cargo:rustc-link-search=native={}", lib_path);
        println!("cargo:rerun-if-env-changed=VIVID_LIB_PATH");
    }
    // Priority 2: Local vivid submodule build
    else if Path::new("../../vivid/build/lib").exists() {
        let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
        let lib_path = Path::new(&manifest_dir).join("../../vivid/build/lib");
        println!("cargo:rustc-link-search=native={}", lib_path.display());
    }
    // Priority 3: System-installed vivid
    else {
        println!("cargo:rustc-link-search=native=/usr/local/lib");
    }

    // Link against vivid-c
    #[cfg(target_os = "macos")]
    println!("cargo:rustc-link-lib=dylib=vivid-c");

    #[cfg(target_os = "windows")]
    println!("cargo:rustc-link-lib=dylib=vivid-c");

    #[cfg(target_os = "linux")]
    println!("cargo:rustc-link-lib=dylib=vivid-c");

    // Include path for headers
    let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
    let include_path = Path::new(&manifest_dir).join("../../vivid/modules/vivid-core/include");
    println!("cargo:include={}", include_path.display());
}
EOF

# Create .gitignore
cat > .gitignore << 'EOF'
# Dependencies
node_modules/
target/
dist/

# Build artifacts
*.dylib
*.so
*.dll

# IDE
.idea/
.vscode/
*.swp

# OS
.DS_Store
Thumbs.db

# Tauri
src-tauri/target/
EOF

# Create README.md
cat > README.md << 'EOF'
# Vivid IDE

Visual creative coding IDE with integrated runtime, node-based chain visualizer,
and built-in terminal for Claude Code.

## Installation

### Option 1: Download IDE (Recommended)

Download the latest release for your platform from the [Releases page](https://github.com/seethroughlab/vivid-ide/releases).

- **macOS (Apple Silicon)**: `Vivid-IDE-vX.X.X-macos-arm64.dmg`
- **macOS (Intel)**: `Vivid-IDE-vX.X.X-macos-x64.dmg`
- **Windows**: `Vivid-IDE-vX.X.X-windows-x64.msi`
- **Linux**: `Vivid-IDE-vX.X.X-linux-x64.AppImage`

### Option 2: Build from Source

```bash
# Clone with submodule
git clone --recursive https://github.com/seethroughlab/vivid-ide.git
cd vivid-ide

# Build the vivid runtime first
cd vivid && cmake -B build && cmake --build build && cd ..

# Install dependencies and build IDE
npm install
npm run tauri build
```

### Option 3: Development Mode

```bash
# Clone with submodule
git clone --recursive https://github.com/seethroughlab/vivid-ide.git
cd vivid-ide

# Build vivid runtime
cd vivid && cmake -B build && cmake --build build && cd ..

# Run in development mode
npm install
npm run tauri dev
```

## Features

- **Visual Chain Editor**: Node-based graph for building operator chains
- **Live Preview**: Real-time rendering with hot-reload on save
- **Monaco Editor**: Full-featured code editor with C++/WGSL syntax highlighting
- **Integrated Terminal**: Built-in terminal for Claude Code integration
- **Parameter Inspector**: Live parameter controls with sliders, color pickers, etc.
- **Keyboard Shortcuts**: Cmd+1/2/3 for panels, Cmd+S to save, etc.

## Requirements

- macOS 10.15+ / Windows 10+ / Linux
- GPU with WebGPU support (Metal on macOS, Vulkan/DX12 on Windows/Linux)

## License

MIT License - See [LICENSE](LICENSE) for details.
EOF

# Create LICENSE
cat > LICENSE << 'EOF'
MIT License

Copyright (c) 2025 See Through Lab

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
EOF

# Initial commit
git add .
git commit -m "Initial vivid-ide repository structure

- Tauri app with Monaco editor, terminal, and inspector
- vivid submodule for runtime (git submodule)
- Build configuration for CI/CD

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"

echo ""
echo "=========================================="
echo "vivid-ide repository created successfully!"
echo "=========================================="
echo ""
echo "Next steps:"
echo "  1. cd $TARGET_DIR"
echo "  2. Build vivid: cd vivid && cmake -B build && cmake --build build && cd .."
echo "  3. Install deps: npm install"
echo "  4. Run dev mode: npm run tauri dev"
echo ""
echo "To push to GitHub:"
echo "  1. Create repo at https://github.com/seethroughlab/vivid-ide"
echo "  2. git remote add origin git@github.com:seethroughlab/vivid-ide.git"
echo "  3. git push -u origin main"
