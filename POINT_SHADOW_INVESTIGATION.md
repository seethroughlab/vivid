# Point Light Shadow Investigation

## Status: UNRESOLVED
Last updated: 2025-01-19

## Summary

Point light cube map shadows are not working correctly. Directional and spot light shadows work fine, but point light shadows fail specifically on the **-Y face (face index 3)** of the cube map.

## The Problem

When point light shadows are enabled:
- Horizontal faces (+X, -X, +Z, -Z) render geometry correctly
- The +Y face works (looking up)
- **The -Y face (looking down, face index 3) does NOT render geometry** - it only shows the clear value

This means shadows are incorrectly applied for any fragment that would sample from the -Y face of the cube map, which includes anything directly below the light.

## What Works

1. **Directional light shadows** - Full 2D shadow map, working correctly
2. **Spot light shadows** - Uses same 2D shadow map system, working correctly
3. **Point light rendering** - Basic point light illumination works without shadows
4. **Cube map faces 0,1,4,5** - These faces render geometry and store depth correctly

## Architecture Overview

### Current Implementation (`renderer.cpp`)

1. **Cube Map Creation** (line ~2090):
   ```cpp
   // R32Float cube map (6 layers) for storing linear depth
   cubeTexDesc.format = WGPUTextureFormat_R32Float;
   cubeTexDesc.size.depthOrArrayLayers = 6;
   ```

2. **Face Views for Rendering** (line ~2107):
   ```cpp
   // 2D view of each face for render target
   faceViewDesc.dimension = WGPUTextureViewDimension_2D;
   faceViewDesc.baseArrayLayer = i;  // face index 0-5
   ```

3. **Cube View for Sampling** (line ~2119):
   ```cpp
   // Cube view for sampling in main pass
   cubeViewDesc.dimension = WGPUTextureViewDimension_Cube;
   cubeViewDesc.arrayLayerCount = 6;
   ```

4. **Shadow Pass Shader** outputs linear depth:
   ```wgsl
   @fragment
   fn fs_main(in: VertexOutput) -> @location(0) f32 {
       let dist = length(in.worldPos - uniforms.lightPos);
       return dist / uniforms.farPlane;  // Normalized to [0,1]
   }
   ```

5. **Main Shader Sampling**:
   ```wgsl
   let lightToFrag = worldPos - shadow.pointLightPos;
   let sampleDir = lightToFrag / fragDist;
   let sampledDepth = textureSample(pointShadowMap, pointShadowSampler, sampleDir).r;
   ```

### Face Matrix Computation (line ~2292)

```cpp
// Cube face directions: +X, -X, +Y, -Y, +Z, -Z
static const glm::vec3 directions[6] = {
    { 1,  0,  0}, {-1,  0,  0},
    { 0,  1,  0}, { 0, -1,  0},
    { 0,  0,  1}, { 0,  0, -1}
};
static const glm::vec3 ups[6] = {
    { 0, -1,  0}, { 0, -1,  0},
    { 0,  0,  1}, { 0,  0, -1},  // Y faces use Z as up
    { 0, -1,  0}, { 0, -1,  0}
};

glm::mat4 view = glm::lookAt(lightPos, lightPos + directions[face], ups[face]);
glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, nearPlane, farPlane);
```

## Verified Working

Through debugging, we confirmed:

1. **Matrix math is correct** - Ground at (0,0,0) with light at (0,4,0) projects to NDC (0, 0, 0.897) for face 3, which is valid center-of-viewport coordinates

2. **Draw calls ARE issued** - Debug output shows:
   ```
   [PointShadow] Drawing object 0 to face 3, vertexBuffer=0x..., indexCount=6
   [PointShadow] Drawing object 1 to face 3, vertexBuffer=0x..., indexCount=36
   ```

3. **Other faces work** - Testing with fixed shader output (0.3) showed green (shader executed) at ground edges where faces 0,1,4,5 sample, but red (clear value 1.0) where face 3 samples

4. **Pipeline configuration is correct**:
   - `cullMode = WGPUCullMode_None`
   - `depthWriteEnabled = true`
   - `depthCompare = WGPUCompareFunction_Less`

## What Was Tried

### 1. R32Float Instead of Depth32Float
**Rationale**: `textureSampleCompare` on cube maps may have issues in wgpu-native
**Result**: Did not help - the issue is with rendering TO face 3, not sampling FROM it

### 2. Sample Direction Negation
**Tried**: `let actualSampleDir = -sampleDir;`
**Result**: No change

### 3. Up Vector Changes
**Tried**: Changed -Y face up vector from (0,0,-1) to (0,0,1)
**Result**: No change

### 4. Explicit Viewport/Scissor
**Tried**:
```cpp
wgpuRenderPassEncoderSetViewport(pass, 0, 0, resolution, resolution, 0, 1);
wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, resolution, resolution);
```
**Result**: No change (still present in code, harmless)

### 5. Face Swapping
**Tried**: Swapped faces 2 and 3 in matrix computation
**Result**: The PROBLEM followed the face INDEX, not the direction - suggesting issue is with array layer 3 specifically

## Key Insights

### The Problem is NOT:
- Matrix computation (verified correct)
- Sample direction (verified correct)
- Culling (disabled)
- Depth test configuration
- Clear values

### The Problem IS:
- Something specific to rendering to array layer index 3 of the cube map
- Possibly a wgpu-native or Metal backend bug
- Or possibly an issue with how 2D views of cube map array layers work

## Theories to Investigate

### 1. wgpu-native Array Layer Bug
There may be a bug in wgpu-native specifically with layer 3 of 2D array textures when used as render targets.

**Test**: Try creating 6 separate 2D textures instead of a 2D array, then manually bind the correct texture based on sample direction.

### 2. Metal Backend Specific Issue
Since this is on macOS with Metal backend, there may be a Metal-specific issue.

**Test**: Test on a different platform (Linux/Vulkan, Windows/DX12) to see if the issue is platform-specific.

### 3. Texture View Dimension Mismatch
The 2D views we create for rendering might not correctly map to cube map array layers.

**Test**: Add extensive validation logging around texture view creation for face 3 specifically.

### 4. Command Buffer Timing
Perhaps the render to face 3 isn't completing before the cube view is sampled.

**Test**: Try inserting a pipeline barrier or splitting into separate command buffers per face.

## Recommended Next Steps

1. **Try 6 separate 2D textures** - This would completely bypass the array layer mechanism

2. **Test on another platform** - If Vulkan/DX12 work, it's a Metal bug

3. **File wgpu-native issue** - Include minimal repro with the diagnostic output showing the matrix is correct but geometry doesn't appear

4. **Alternative approach** - Consider dual-paraboloid shadow maps instead of cube maps as a workaround

## Files Modified

| File | Purpose |
|------|---------|
| `addons/vivid-render3d/src/renderer.cpp` | Main shadow implementation |
| `addons/vivid-render3d/include/vivid/render3d/renderer.h` | Member variables |

## Test Scene

A simple test scene exists at `/tmp/point-debug/chain.cpp`:
- Ground plane at y=0
- Cube at y=0.75
- Point light at y=4 with shadows enabled

Comparison scene at `/tmp/point-no-shadow/chain.cpp`:
- Same scene with `castShadow(false)` and `setShadows(false)`

## Debug Visualization

To visualize which cube map faces are being sampled, modify the Flat shading mode in `renderer.cpp` (line ~353):

```wgsl
} else if (uniforms.shadingMode == 1u) {
    // DEBUG: Visualize cube map sampling
    let lightToFrag = in.worldPos - shadow.pointLightPos;
    let fragDist = length(lightToFrag);
    let sampleDir = lightToFrag / fragDist;
    let sampledDepth = textureSample(pointShadowMap, pointShadowSampler, sampleDir).r;

    // Green = shader ran, Red = clear value only
    if (sampledDepth < 0.5) {
        return vec4f(0.0, 1.0, 0.0, 1.0);
    }
    return vec4f(1.0, 0.0, 0.0, 1.0);
}
```

And modify the point shadow shader (line ~2200) to output fixed 0.3:
```wgsl
@fragment
fn fs_main(in: VertexOutput) -> @location(0) f32 {
    return 0.3;  // Fixed value to distinguish from clear value (1.0)
}
```

This produces red everywhere face 3 would be sampled (center of ground) and green elsewhere (edges where other faces sample).

## Current State

The code is reverted to a "working but incorrect" state:
- Point light shadows are attempted but don't work correctly
- The scene appears darker than expected because face 3 returns clear value (1.0), causing everything below the light to be in shadow
- To **disable point shadows entirely** until fixed, set `light.castShadow(false)` or check for point lights in `hasPointLightShadow()` and return false

## External Evidence (wgpu GitHub Issues)

Research found several related issues in the wgpu repository that support our findings:

### 1. Multi-Layer Texture Rendering Bug ([#1690](https://github.com/gfx-rs/wgpu/issues/1690))
**Directly relevant to our problem:**
- When rendering a cube map using a single 6-layer texture, **only layer 0 rendered correctly**
- Other layers showed corrupted/garbage output
- Platform: Vulkan on AMD RX 590
- **Documented workaround: Use 6 separate single-layer textures instead of one 6-layer texture**
- Status: Eventually marked resolved (reporter couldn't reproduce later), but the pattern matches our symptoms

### 2. WebGL2 Point Light Shadow Issues ([#2138](https://github.com/gfx-rs/wgpu/issues/2138))
- Point light shadow maps had issues in Firefox/WebGL2
- First point light worked, others didn't
- Related to cube array texture support limitations

### 3. Cube Map Shadow Map Fix ([PR #2143](https://github.com/gfx-rs/wgpu/pull/2143))
- Confirms there was a real bug with cube map shadow maps in wgpu
- Fix was due to a misread of the GL ES specification
- Shows this class of bug has existed before

### 4. Depth Texture Sampling Limitations ([#4524](https://github.com/gfx-rs/wgpu/issues/4524))
- `textureSampleCompare` only returns 0.0 or 1.0, not gradient values
- Validates our approach of using R32Float with manual comparison instead of Depth32Float with textureSampleCompare

### 5. Layer Selection in Vertex Shaders ([#1475](https://github.com/gfx-rs/wgpu/issues/1475))
- Discusses challenges with rendering to multiple cube map layers
- Mentions `VK_EXT_shader_viewport_index_layer` as potential solution for single-pass cube map rendering

## Recommended Fix Based on External Evidence

**Primary recommendation:** Implement 6 separate 2D textures instead of a 6-layer array texture.

This was the documented workaround in [#1690](https://github.com/gfx-rs/wgpu/issues/1690) and completely bypasses the array layer mechanism that appears to be problematic.

Implementation approach:
```cpp
// Instead of:
WGPUTexture m_pointShadowMapTexture;  // 6-layer array
WGPUTextureView m_pointShadowFaceViews[6];  // Views into layers

// Use:
WGPUTexture m_pointShadowFaceTextures[6];  // 6 separate textures
WGPUTextureView m_pointShadowFaceViews[6];  // Views of each texture
```

For sampling, manually determine which texture to sample based on the dominant axis of the sample direction vector, rather than relying on cube map hardware sampling.

## Related Issues

- The same cube map implementation pattern might have issues with +Y face (face 2) in other scenarios - worth testing if needed for ceiling lights
