# Vivid VS Code Extension

VS Code support for the Vivid creative coding framework.

## Features

- **Compile Error Diagnostics**: GCC/Clang errors shown in Problems panel with jump-to-location
- **Status Bar**: Connection status indicator
- **Inline Decorations**: Live values shown next to code (`~ 3.14`, `[img]`, etc.)
- **WGSL Language Support**: Syntax highlighting for shader files
- **Runtime Commands**: Start/stop runtime, force reload

## Installation

```bash
cd extension
npm install
npm run compile
```

Then press F5 in VS Code to launch the extension in development mode.

## Commands

- `Vivid: Start Runtime` - Launch the Vivid runtime
- `Vivid: Stop Runtime` - Disconnect from runtime
- `Vivid: Force Reload` - Trigger chain reload
- `Vivid: Toggle Inline Decorations` - Show/hide inline value previews

## Configuration

- `vivid.runtimePath` - Path to vivid executable
- `vivid.websocketPort` - WebSocket port (default: 9876)
- `vivid.showInlineDecorations` - Enable inline decorations (default: true)
- `vivid.previewSize` - Thumbnail size in pixels (default: 48)
- `vivid.autoConnect` - Auto-connect when opening Vivid project (default: true)

## Note

The node graph visualization is now built into the Vivid runtime itself (using ImGui/imnodes), so this extension focuses on editor integration features only.
