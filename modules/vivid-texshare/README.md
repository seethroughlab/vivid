# vivid-texshare

Texture sharing between Vivid and other applications using Syphon (macOS) and Spout (Windows).

## Overview

This module enables real-time GPU texture sharing between Vivid and other applications such as:

- **VJ Software**: Resolume Arena/Avenue, VDMX, Modul8
- **Creative Tools**: TouchDesigner, Processing, openFrameworks
- **Media Servers**: MadMapper, Millumin, Isadora
- **Other Vivid instances**: Run multiple Vivid chains and share textures between them

## Platform Support

| Platform | Framework | Status |
|----------|-----------|--------|
| macOS | Syphon | Implemented |
| Windows | Spout | Stub (planned) |
| Linux | - | Not supported |

## Operators

### TextureShareOut

Publishes textures to a named server that other applications can connect to.

```cpp
#include <vivid/texshare/texshare.h>

auto& noise = chain.add<Noise>("noise");

auto& share = chain.add<TextureShareOut>("share");
share.input("noise");
share.serverName = "My Vivid Output";

chain.output("share");  // Pass-through to display
```

**Parameters:**
- `serverName` (string, default: "Vivid"): Server name visible to other applications

**Notes:**
- Pass-through operator: forwards input to output unchanged
- Server starts automatically on init
- Server name can be changed at runtime

### TextureShareIn

Receives textures from a named server published by another application.

```cpp
#include <vivid/texshare/texshare.h>

auto& recv = chain.add<TextureShareIn>("recv");
recv.serverName = "Resolume Arena";

auto& blur = chain.add<Blur>("blur");
blur.input("recv");

chain.output("blur");
```

**Parameters:**
- `serverName` (string, default: ""): Name of server to connect to

**Methods:**
- `availableServers()`: Returns list of discovered texture servers
- `isConnected()`: Returns true if receiving textures
- `getReceivedSize(width, height)`: Get dimensions of received texture

## Server Discovery

List available texture servers at runtime:

```cpp
auto& recv = chain.add<TextureShareIn>("recv");

// List servers
auto servers = recv.availableServers();
for (const auto& server : servers) {
    std::cout << server.name << " from " << server.appName << std::endl;
}

// Connect to first server
if (!servers.empty()) {
    recv.serverName = servers[0].name;
}
```

## Examples

### texture-sharing

Basic example that shares animated noise with other applications.

```bash
./build/bin/vivid modules/vivid-texshare/examples/texture-sharing
```

Controls:
- `1`: List available servers
- `2`: Toggle sharing on/off
- `R`: Reset animation

## Dependencies

### macOS (Syphon)

The Syphon framework is **automatically downloaded** during CMake configuration if not already installed. No manual installation required.

CMake will check these locations first:
- `/Library/Frameworks/Syphon.framework`
- `~/Library/Frameworks/Syphon.framework`

If not found, it downloads the pre-built framework from the [Syphon GitHub releases](https://github.com/Syphon/Syphon-Framework/releases).

### Windows (Spout)

Spout support is currently a stub. Full implementation planned for a future release.

## Technical Notes

### Texture Format

- **Output (TextureShareOut)**: Textures are published in RGBA8 format
- **Input (TextureShareIn)**: Received textures are in BGRA8 format (converted to RGBA internally)

### Performance

The current implementation uses CPU staging buffers for texture transfer between WebGPU and Syphon/Spout. This is reliable but not zero-copy.

Future improvements may include:
- IOSurface interop for zero-copy Syphon sharing
- DirectX shared handles for Spout

### Thread Safety

- Server directory updates occur on background threads
- Frame callbacks are thread-safe
- All WebGPU operations occur on the main thread

## Troubleshooting

### macOS: Server not visible in other apps

1. Ensure the Syphon framework is properly linked
2. Check that the server name doesn't conflict with another application
3. Verify the receiving application supports Syphon Metal (vs legacy OpenGL)

### Windows: Spout not working

Spout support is currently a stub implementation. Full support coming soon.

### Connection drops

- Servers may disconnect when the source application is closed
- Use `isConnected()` to detect disconnection
- The fallback texture (black) is shown when not connected
