# Shadow Mapping Debug Notes

## Problem Statement

When shadows are enabled with directional lights, the entire scene goes completely dark (everything appears "in shadow"). The goal is proper shadow mapping where objects cast visible shadows while the scene remains properly lit.

## Architecture Overview

The shadow system uses:
- **DiligentFX `PBR_Renderer`** - Handles PBR shading with shadow support
- **`ShadowMapManager`** - Manages cascaded shadow maps (4 cascades, 2048x2048, PCF filtering)
- **`PBRShadowMapInfo`** - Structure passed to shaders containing:
  - `WorldToLightProjSpace` - Transform from world to light projection space
  - `UVScale/UVBias` - Shadow map UV coordinate adjustments
  - `ShadowMapSlice` - Which cascade slice to sample

## What We've Tried

### 1. Initial Problem: Using ShadowMapAttribs vs PBRShadowMapInfo
- **Issue**: Was filling wrong shadow structure in FrameAttribs
- **Fix**: Changed to use `PBRShadowMapInfo` structure instead of `ShadowMapAttribs`
- **Result**: Fixed "dark rendering" but shadows still not visible

### 2. Bug: Passing IBL Environment Map as Shadow Map
- **Issue**: `InitCommonSRBVars` 5th parameter is `pShadowMap`, NOT environment map
- **Original code**:
  ```cpp
  pbrRenderer_->InitCommonSRBVars(currentSrb, frameAttribsBuffer_, true, true, prefilteredEnvMap);
  ```
- **Fix**:
  ```cpp
  ITextureView* shadowMapSRV = shadowMapManager_->GetSRV();
  pbrRenderer_->InitCommonSRBVars(currentSrb, frameAttribsBuffer_, true, true, shadowMapSRV);
  ```
- **Result**: No visible change - scene still dark with shadows

### 3. Missing IBL Texture Bindings
- **Issue**: After removing IBL map from InitCommonSRBVars, IBL wasn't bound
- **Fix**: Added explicit bindings:
  ```cpp
  if (auto* var = currentSrb->GetVariableByName(SHADER_TYPE_PIXEL, "g_IrradianceMap")) {
      var->Set(environment_->irradianceSRV());
  }
  if (auto* var = currentSrb->GetVariableByName(SHADER_TYPE_PIXEL, "g_PrefilteredEnvMap")) {
      var->Set(environment_->prefilteredSRV());
  }
  ```
- **Result**: No visible change

### 4. Manual Light Matrix Computation vs ShadowMapManager Cascades
- **Issue**: Computing light view-projection manually might have wrong depth conventions
- **Manual approach** (lines 918-928):
  ```cpp
  glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0), up);
  glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, 100.0f);
  ```
- **Fix**: Use ShadowMapManager's cascade transform:
  ```cpp
  const auto& cascadeTransform = shadowMapManager_->GetCascadeTransform(0);
  worldToLightProjSpace = cascadeTransform.WorldToLightProjSpace;
  ```
- **Result**: Applied to main pass only, shadow pass still uses manual matrices

## Current State

### Main Pass (sampling shadows)
- Uses `cascadeTransform.WorldToLightProjSpace` from ShadowMapManager
- Fills `PBRShadowMapInfo` with correct UVScale, UVBias, ShadowMapSlice
- Binds correct shadow map SRV from `shadowMapManager_->GetSRV()`

### Shadow Pass (rendering depth)
- **Still uses manual matrix computation** - NOT using cascade transform
- Creates `shadowFrameAttribsBuffer_` with light's view-projection
- May have depth range mismatch (GLM [-1,1] vs Vulkan [0,1])

## Current Hypothesis

**The shadow pass and main pass are using different projection matrices.**

The main pass now uses `ShadowMapManager::GetCascadeTransform(0).WorldToLightProjSpace`, but the shadow pass still computes its own matrices manually. This mismatch means:
1. Shadow pass renders depth at positions X
2. Main pass looks up shadow at positions Y
3. Result: Everything is "in shadow" because lookups don't match rendered depth

### 5. Shadow Pass Using Manual Matrices Instead of Cascade Transform
- **Issue**: Shadow pass computed its own light view-projection matrices manually
- **Original code** (lines 918-928):
  ```cpp
  glm::vec3 lightDirGlm = glm::normalize(shadowLight.direction);
  glm::vec3 up = (std::abs(lightDirGlm.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 right = glm::normalize(glm::cross(up, lightDirGlm));
  up = glm::normalize(glm::cross(lightDirGlm, right));
  glm::vec3 lightPos = -lightDirGlm * 50.0f;
  glm::mat4 lightView = glm::lookAt(lightPos, glm::vec3(0), up);
  float orthoSize = 15.0f;
  glm::mat4 lightProj = glm::ortho(-orthoSize, orthoSize, -orthoSize, orthoSize, 0.1f, 100.0f);
  glm::mat4 lightViewProj = lightProj * lightView;
  ```
- **Fix**: Use ShadowMapManager's cascade transform with View * Proj pattern from DiligentFX samples:
  ```cpp
  const auto& cascadeTransform = shadowMapManager_->GetCascadeTransform(cascade);
  float4x4 lightViewProjDiligent = worldToLightView * cascadeTransform.Proj;
  ```
- **Result**: Compile error - `shadowMapAttribs` was local to `renderScene()` but used in `renderShadowPass()`

### 6. Scope Issue - shadowMapAttribs Local Variable
- **Issue**: `shadowMapAttribs` was declared locally in `renderScene()` at line 241, but `renderShadowPass()` needed access to `mWorldToLightView`
- **Error**:
  ```
  error: use of undeclared identifier 'shadowMapAttribs'
  float4x4 lightViewProjDiligent = shadowMapAttribs.mWorldToLightView * cascadeTransform.Proj;
  ```
- **Fix**: Pass `mWorldToLightView` as parameter to `renderShadowPass()`:
  1. Updated header signature:
     ```cpp
     void renderShadowPass(Context& ctx, const Diligent::float4x4& worldToLightView);
     ```
  2. Updated call site in `renderScene()`:
     ```cpp
     renderShadowPass(ctx, shadowMapAttribs.mWorldToLightView);
     ```
  3. Updated function definition to use parameter:
     ```cpp
     float4x4 lightViewProjDiligent = worldToLightView * cascadeTransform.Proj;
     ```
- **Result**: Compiles and runs - needs visual testing to confirm shadows work

### 7. Buffer Alignment for 4 Light Slots
- **Issue**: Light buffer might not be properly sized for 4 shadow-casting light slots
- **Fix**: Ensured buffer allocation for 4 `PBRShadowMapInfo` structures
- **Result**: No visible change

### 8. Matrix Transposition Experiments

Multiple attempts were made to address matrix conventions:

#### Attempt A: Adding .Transpose() to Shadow Pass Matrices
- **Hypothesis**: Shadow pass matrices need transposition for column-major HLSL
- **Change**: Added `.Transpose()` to shadow pass camera matrices:
  ```cpp
  camera->mViewProj = cascadeTransform.WorldToLightProjSpace.Transpose();
  camera->mView = worldToLightView.Transpose();
  camera->mProj = cascadeTransform.Proj.Transpose();
  ```
- **Result**: No change - scene still dark

#### Attempt B: Adding .Transpose() to Main Pass Shadow Info
- **Hypothesis**: WorldToLightProjSpace in PBRShadowMapInfo also needs transposition
- **Change**:
  ```cpp
  shadowInfo->WorldToLightProjSpace = worldToLightProjSpace.Transpose();
  ```
- **Result**: No change - scene still dark

#### Attempt C: Removing ALL .Transpose() Calls (Current State)
- **Hypothesis**: Main camera uses `ToFloat4x4(glm_mat)` without explicit transpose, shadow pass should match
- **Analysis**: `ToFloat4x4()` does `result[row][col] = m[col][row]` which already handles GLM→Diligent conversion. Native Diligent matrices shouldn't need additional transposition.
- **Change**: Removed all `.Transpose()` calls:
  ```cpp
  // Shadow pass
  camera->mViewProj = cascadeTransform.WorldToLightProjSpace;
  camera->mView = worldToLightView;
  camera->mProj = cascadeTransform.Proj;

  // Main pass shadow info
  shadowInfo->WorldToLightProjSpace = worldToLightProjSpace;
  ```
- **Result**: Still dark (needs verification)

## Understanding the Problem (Updated Dec 2024)

### What We Know
1. **Scene renders correctly with shadows disabled** - Lighting, materials, IBL all work
2. **Scene goes completely dark with shadows enabled** - Not partially shadowed, but entirely black
3. **Multiple matrix convention attempts failed** - Both with and without transposition

### Matrix Convention Analysis

The codebase has two matrix sources:
1. **GLM matrices** (column-major) - Used for main camera, converted via `ToFloat4x4()`
2. **Diligent matrices** (row-major) - Used by ShadowMapManager, returned directly

The `ToFloat4x4` function does index swapping:
```cpp
result[row][col] = m[col][row];  // Effectively transposes GLM→Diligent
```

The question is: Do native Diligent matrices from ShadowMapManager need the same treatment?

### DiligentFX Shadows Sample Reference

The official sample uses `PackMatrixRowMajor` setting and `WriteShaderMatrix`:
```cpp
const float4x4& WorldToLightViewSpaceMatr = m_PackMatrixRowMajor ?
    m_LightAttribs.ShadowAttribs.mWorldToLightView :
    m_LightAttribs.ShadowAttribs.mWorldToLightView.Transpose();
```

Our code doesn't use `WriteShaderMatrix` - we directly assign matrices. This may be the issue.

### Root Cause Hypotheses (Ordered by Likelihood)

1. **Shadow depth comparison always fails** - Fragment depth vs shadow map depth comparison never passes, making everything "in shadow"
2. **Shadow map is empty/not rendered** - The shadow pass may not be rendering correctly
3. **UVScale/UVBias incorrect** - Shadow UV lookups map to wrong locations
4. **Shader-side issue** - The PBR shader's shadow calculation has a bug

## Suggested Alternative Approaches

### Approach 1: Debug Visualization
Add shader output to visualize:
- Raw shadow map depth values
- Fragment depth in light space
- Shadow UV coordinates
- Whether the comparison function is working

### Approach 2: Minimal Shadow Implementation
Strip down to the simplest possible shadow:
- Single light, single cascade
- No PCF filtering
- Hardcoded matrices
- Step-by-step verification

### Approach 3: Study GLTF Viewer Implementation
The DiligentFX GLTF Viewer sample has working shadows. Compare its shadow setup directly.

### Approach 4: Use ShadowMapManager's High-Level API
Instead of manually filling `PBRShadowMapInfo`, use whatever high-level API ShadowMapManager provides for shader setup.

### 9. WriteShaderMatrix Fix (Dec 2024)
- **Issue**: DiligentFX samples use `WriteShaderMatrix` for proper row/column major handling
- **Fix**: Applied `WriteShaderMatrix(&matrix, source, true)` for both shadow pass camera matrices and main pass `PBRShadowMapInfo::WorldToLightProjSpace`
- **Result**: No change - scene still completely dark with shadows enabled

### Additional Investigation (Dec 2024)

Deep analysis of the DiligentFX shadow system revealed:

1. **Sampler Comparison Function**: `Sam_ComparisonLinearClamp` uses `COMPARISON_FUNC_LESS` - fragment depth < shadow map depth = lit
2. **NormalizedDeviceZToDepth**: For HLSL/Vulkan this is a pass-through function (returns input directly)
3. **NormalizedDeviceXYToTexUV**: Converts NDC XY to texture UV: `float2(0.5,0.5) + float2(0.5,-0.5) * f2ProjSpaceXY.xy`
4. **Orthographic Projection**: For directional lights, uses orthographic projection where w=1.0 (no perspective divide)
5. **Shadow Map Texture**: Created as `RESOURCE_DIM_TEX_2D_ARRAY` with depth format, cleared to 1.0

The shadow sampling code in `PBR_Shading.fxh` (lines 644-660):
```hlsl
if (Light.ShadowMapIndex >= 0)
{
    float4 ShadowPos = mul(float4(Shading.Pos, 1.0), ShadowMapInfo.WorldToLightProjSpace);
    ShadowPos.xy /= ShadowPos.w;
    ShadowPos.xy = NormalizedDeviceXYToTexUV(ShadowPos.xy) * ShadowMapInfo.UVScale + ShadowMapInfo.UVBias;
    ShadowPos.z  = NormalizedDeviceZToDepth(ShadowPos.z);
    // ... PCF filtering ...
}
```

All verified aspects appear correct, yet shadows remain broken.

## Current State (Dec 2024)

**Status: PAUSED - Shadows disabled in all examples**

The code compiles and runs, but all shadow mapping attempts have failed. We've tried:
- Different shadow info structures (ShadowMapAttribs vs PBRShadowMapInfo)
- Different matrix sources (manual computation vs ShadowMapManager)
- Different transposition combinations (none, partial, full)
- Ensuring consistent matrices between shadow pass and main pass
- Using `WriteShaderMatrix` for proper row/column major handling

None of these changes have made a visible difference. The problem appears to be more fundamental than matrix conventions.

**Shadows have been disabled in all example applications until this issue is resolved.**

## Key Files

- `/Users/jeff/Developer/vivid/runtime/src/operators/render3d.cpp` - Main implementation
- `/Users/jeff/Developer/vivid/runtime/include/vivid/operators/render3d.h` - Header
- `/Users/jeff/Developer/vivid/external/DiligentEngine/DiligentFX/PBR/interface/PBR_Renderer.hpp` - PBR renderer interface
- `/Users/jeff/Developer/vivid/external/DiligentEngine/DiligentFX/Shaders/PBR/public/PBR_Shading.fxh` - Shader code

## Relevant DiligentFX Structures

```cpp
// PBRShadowMapInfo - what gets passed to shader
struct PBRShadowMapInfo {
    float4x4 WorldToLightProjSpace;  // Full transform from world to light clip space
    float4   UVScale;                 // Shadow map UV scale
    float4   UVBias;                  // Shadow map UV bias
    int      ShadowMapSlice;          // Cascade index
};

// CascadeTransforms - from ShadowMapManager
struct CascadeTransforms {
    float4x4 WorldToLightProjSpace;  // The full world-to-light-projection transform
    float4x4 Proj;                   // Just the projection part
    // ... other fields
};
```
