# Vivid Installation Guide

This guide covers installing Vivid on macOS, Linux, and Windows.

## Prerequisites

### All Platforms
- **CMake 3.16+** — Build system
- **C++20 compiler** — Clang 14+, GCC 11+, or MSVC 2022+
- **Node.js 18+** — For the VS Code extension
- **VS Code** — Recommended IDE

### macOS
```bash
# Install Xcode command line tools (includes clang)
xcode-select --install

# Install CMake via Homebrew
brew install cmake
```

### Linux (Ubuntu/Debian)
```bash
# Install build essentials and CMake
sudo apt update
sudo apt install build-essential cmake

# Install graphics libraries
sudo apt install libglfw3-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

### Windows
1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with "Desktop development with C++" workload
2. Install [CMake](https://cmake.org/download/) (add to PATH during installation)
3. Install [Node.js](https://nodejs.org/) (LTS version recommended)

## Quick Start

### Using Build Scripts

The easiest way to build everything:

**macOS:**
```bash
git clone https://github.com/vivid-framework/vivid.git
cd vivid
./scripts/build-macos.sh
```

**Linux:**
```bash
git clone https://github.com/vivid-framework/vivid.git
cd vivid
./scripts/build-linux.sh
```

**Windows (from Developer Command Prompt):**
```cmd
git clone https://github.com/vivid-framework/vivid.git
cd vivid
scripts\build-windows.bat
```

### Manual Build

#### Step 1: Build the Runtime

```bash
git clone https://github.com/vivid-framework/vivid.git
cd vivid

# Configure with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build (use -j for parallel compilation)
cmake --build build -j8
```

The runtime executable will be at:
- macOS/Linux: `build/bin/vivid-runtime`
- Windows: `build/bin/Release/vivid-runtime.exe`

#### Step 2: Build the VS Code Extension

```bash
cd extension
npm install
npm run compile
```

#### Step 3: Build the Native Module (for shared memory previews)

```bash
cd extension/native
npm install
npm run build
```

## Testing the Installation

### Run the Hello Example

```bash
# From the vivid directory
./build/bin/vivid-runtime examples/hello
```

You should see a window with animated noise.

### Test the VS Code Extension

1. Open the `extension` folder in VS Code
2. Press `F5` to launch Extension Development Host
3. In the new window, open the `examples/hello` folder
4. Open `chain.cpp` — you should see `[img]` decorations
5. Run the runtime (see above)
6. The preview panel should show live updates

## Installing the Extension

### From .vsix File

```bash
# Build the .vsix package
cd extension
npm run package

# Install in VS Code
code --install-extension vivid-0.1.0.vsix
```

### Development Mode

For active development, run from source:
1. Open `extension` folder in VS Code
2. Press `F5` to launch Extension Development Host

## Configuration

### VS Code Settings

The extension provides these settings:

| Setting | Default | Description |
|---------|---------|-------------|
| `vivid.runtimePath` | `""` | Path to vivid-runtime executable |
| `vivid.websocketPort` | `9876` | WebSocket port for runtime communication |
| `vivid.showInlineDecorations` | `true` | Show inline preview decorations |
| `vivid.previewSize` | `48` | Thumbnail size in pixels |
| `vivid.autoConnect` | `true` | Auto-connect when opening Vivid projects |

### Setting the Runtime Path

If vivid-runtime isn't in your PATH, set it in VS Code settings:

```json
{
  "vivid.runtimePath": "/path/to/vivid/build/bin/vivid-runtime"
}
```

## Project Structure

After building, your Vivid directory looks like:

```
vivid/
├── build/
│   ├── bin/
│   │   └── vivid-runtime      # The runtime executable
│   ├── include/               # Headers for operators
│   └── shaders/               # Copied shaders
├── extension/
│   ├── out/                   # Compiled extension
│   ├── native/build/          # Native module
│   └── vivid-0.1.0.vsix       # Packaged extension
├── examples/
│   ├── hello/                 # Basic example
│   ├── feedback/              # Feedback trails
│   ├── gradient-blend/        # Gradient compositing
│   ├── lfo-modulation/        # LFO-driven visuals
│   └── shapes/                # SDF shapes
└── docs/                      # Documentation
```

## Creating Your First Project

See the [Operator API Guide](OPERATOR-API.md) for detailed instructions on creating custom operators.

Quick setup:

```bash
mkdir my-project
cd my-project

# Copy CMakeLists.txt from an example
cp ../examples/hello/CMakeLists.txt .

# Create your operator
cat > chain.cpp << 'EOF'
#include <vivid/vivid.h>

class MyOperator : public vivid::Operator {
    vivid::Texture output_;
public:
    void init(vivid::Context& ctx) override {
        output_ = ctx.createTexture();
    }
    void process(vivid::Context& ctx) override {
        ctx.runShader("shaders/noise.wgsl", nullptr, output_);
        ctx.setOutput("out", output_);
    }
    vivid::OutputKind outputKind() override { return vivid::OutputKind::Texture; }
};
VIVID_OPERATOR(MyOperator)
EOF

# Run it
/path/to/vivid-runtime .
```

## Troubleshooting

### Runtime won't start

- **macOS**: Make sure you've approved the app in System Preferences > Security & Privacy
- **Linux**: Check that required libraries are installed (libglfw, libX11)
- **Windows**: Run from a Visual Studio Developer Command Prompt

### Extension not connecting

1. Check that the runtime is running and listening on port 9876
2. Verify `vivid.websocketPort` matches the runtime port
3. Check VS Code Output panel for "Vivid" channel

### Previews not updating

1. Ensure the native module built successfully (`extension/native/build/Release/shared_preview.node`)
2. If shared memory fails, the extension falls back to WebSocket (slower but works)
3. Check that operators are calling `ctx.setOutput("out", texture)`

### Compile errors not showing

1. Make sure the extension is connected (check status bar)
2. Open a `.cpp` file in the project
3. Errors appear as diagnostics after saving

## Updating

To update to a new version:

```bash
cd vivid
git pull
./scripts/build-macos.sh  # or appropriate platform script
```

For the extension, rebuild and reinstall:

```bash
cd extension
npm run package
code --install-extension vivid-0.1.0.vsix --force
```
