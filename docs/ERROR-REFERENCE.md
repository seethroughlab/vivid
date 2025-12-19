# Error Reference

Common errors encountered when developing with Vivid, with causes and solutions.

## Compile Errors

### Operator Not Found

**Symptom:**
```
error: 'Noize' was not declared in this scope
```

**Cause:** Typo in operator name or missing include.

**Fix:** Check spelling against LLM-REFERENCE.md. Common typos:
- `Noize` → `Noise`
- `Blurr` → `Blur`
- `Composit` → `Composite`

If spelling is correct, add the required include:
```cpp
#include <vivid/effects/noise.h>
```

---

### Missing VIVID_CHAIN Macro

**Symptom:**
```
error: undefined reference to 'vivid_setup'
error: undefined reference to 'vivid_update'
```

**Cause:** The `VIVID_CHAIN` macro is missing at the end of chain.cpp.

**Fix:** Add the macro after your setup and update functions:
```cpp
void setup(Context& ctx) { /* ... */ }
void update(Context& ctx) { /* ... */ }
VIVID_CHAIN(setup, update)
```

---

### Wrong Parameter Type

**Symptom:**
```
error: cannot convert 'const char*' to 'float'
error: no matching function for call to 'max(float, Param<float>&)'
```

**Cause:** Param<T> values need explicit casting in some contexts.

**Fix:** Use explicit casts when passing to std:: functions:
```cpp
// Wrong
std::max(0.0f, m_param)

// Right
std::max(0.0f, static_cast<float>(m_param))
```

---

### Ambiguous Operator Call

**Symptom:**
```
error: call of overloaded 'add<Noise>(const char*)' is ambiguous
```

**Cause:** Multiple operators with similar names or template issues.

**Fix:** Fully qualify the operator type:
```cpp
chain.add<vivid::Noise>("noise");
```

---

### Missing Context Reference

**Symptom:**
```
error: 'ctx' was not declared in this scope
```

**Cause:** Accessing `ctx` outside of setup/update functions.

**Fix:** Only use `ctx` within the functions that receive it:
```cpp
void setup(Context& ctx) {
    // ctx is valid here
}
```

---

## Runtime Errors

### Shader Not Found

**Symptom:**
```
[ERROR] Failed to load shader: shaders/blur.wgsl
```

**Cause:** Shader files are missing from the build directory.

**Fix:** Ensure shaders are copied during build. Check that `build/shaders/` contains the required .wgsl files. If missing, re-run:
```bash
cmake --build build
```

---

### Image/Video Not Found

**Symptom:**
```
[ERROR] Failed to load image: assets/texture.png
```

**Cause:** Asset path is incorrect or file doesn't exist.

**Fix:**
1. Check the path is relative to the project directory
2. Verify the file exists: `ls assets/texture.png`
3. Use absolute paths if needed during development

---

### GPU Initialization Failed

**Symptom:**
```
[ERROR] Failed to create WebGPU adapter
[ERROR] No suitable GPU found
```

**Cause:** GPU drivers are outdated or WebGPU isn't supported.

**Fix:**
- **macOS:** Update to macOS 11+ (Metal required)
- **Windows:** Update GPU drivers, ensure Vulkan 1.1+ support
- **Linux:** Install Vulkan drivers: `apt install mesa-vulkan-drivers`

---

### Hot-Reload Crash

**Symptom:**
```
[ERROR] Hot-reload failed: compilation error
Segmentation fault
```

**Cause:** The hot-reloaded code has a runtime error or the dylib failed to load.

**Fix:**
1. Check the console for compile errors before the crash
2. Simplify your chain to isolate the problem
3. Restart vivid if the process is corrupted

---

## Chain Errors

### No Output Specified

**Symptom:**
```
[WARNING] No output operator found in chain
```
Black screen or no rendering.

**Cause:** Chain doesn't specify which operator to display.

**Fix:** Call `chain.output()` with the name of the operator to display:
```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Noise>("noise");
    chain.output("noise");  // Required!
}
```

---

### Circular Dependency

**Symptom:**
```
[ERROR] Circular dependency detected: A -> B -> A
```

**Cause:** Operators form a loop in their connections.

**Fix:** Break the cycle. If you need feedback loops, use the `Feedback` operator which handles frame delays:
```cpp
auto& feedback = chain.add<Feedback>("feedback");
feedback.input(someOp);
someOp.input(feedback);  // This works with Feedback
```

---

### Operator Not Cooking

**Symptom:** Operator output doesn't update even when inputs change.

**Cause:** Operator isn't marked as dirty or has caching issues.

**Fix:**
1. Ensure time-dependent operators have animation enabled
2. For custom operators, call `markDirty()` when state changes
3. Check that inputs are properly connected

---

## Audio Errors

### No Audio Output

**Symptom:** Audio operators don't produce sound.

**Cause:** Audio system not initialized or output not connected.

**Fix:**
1. Ensure an `AudioOut` operator exists in the chain
2. Check system audio settings
3. Verify audio addon is linked (check for vivid-audio in build)

---

### Audio Clicks/Pops

**Symptom:** Audio has audible glitches.

**Cause:** Buffer underrun or audio thread starvation.

**Fix:**
1. Increase buffer size in audio configuration
2. Reduce CPU load in update() function
3. Move heavy computation out of audio callbacks

---

## Video Errors

### Video Playback Black

**Symptom:** VideoPlayer shows black instead of video content.

**Cause:** Codec not supported or path incorrect.

**Fix:**
1. Verify video file exists and path is correct
2. Check codec support:
   - **macOS:** H.264, HEVC, ProRes, HAP supported
   - **Windows:** H.264, HEVC via Media Foundation
   - **Linux:** Depends on FFmpeg installation
3. Try a different video format (H.264 is most compatible)

---

### HAP Codec Not Working

**Symptom:** HAP videos show errors or play as standard codec.

**Cause:** HAP decoder not available on platform.

**Fix:**
- **macOS:** HAP is fully supported, should work automatically
- **Windows/Linux:** HAP requires FFmpeg; falls back to standard codecs

---

## 3D Rendering Errors

### Models Not Visible

**Symptom:** GLTF models load but don't appear.

**Cause:** Camera not pointed at model or scale issues.

**Fix:**
1. Check camera position: `camera.position(0, 0, 5)`
2. Check model scale - GLTF units may differ
3. Ensure Render3D operator is connected to output

---

### Textures Missing on Models

**Symptom:** 3D models appear with wrong or missing textures.

**Cause:** Texture paths in GLTF are relative and not found.

**Fix:**
1. Place textures in same directory as GLTF
2. Use GLB format (embedded textures)
3. Check console for texture loading errors

---

## Network Errors

### OSC Not Receiving

**Symptom:** OSCIn operator doesn't receive messages.

**Cause:** Port conflict or firewall blocking.

**Fix:**
1. Check port isn't in use: `lsof -i :8000`
2. Try a different port
3. Check firewall settings allow UDP on the port

---

### WebSocket Connection Failed

**Symptom:** VSCode extension can't connect to vivid.

**Cause:** Wrong port or vivid not running.

**Fix:**
1. Ensure vivid is running with a project
2. Default WebSocket port is 9876
3. Check for port conflicts: `lsof -i :9876`

---

## Debugging Tips

### Enable Debug Logging

Set environment variable before running:
```bash
VIVID_DEBUG_CHAIN=1 ./build/bin/vivid examples/my-project
```

Or in code:
```cpp
ctx.chain().setDebug(true);
```

### Use Snapshot Mode for Testing

Capture a frame to verify output:
```bash
./build/bin/vivid my-project --snapshot test.png --snapshot-frame 10
```

### Check Chain Visualizer

Press **Tab** to open the node graph visualizer:
- Verify all connections are correct
- Check operator thumbnails for expected output
- Select nodes to inspect parameters

### Isolate the Problem

Comment out operators to find the issue:
```cpp
// chain.add<ProblematicOp>("problem");
```

Then re-enable one at a time until the error appears.
