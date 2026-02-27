# Tier 4: Complex Operator Ports

Multi-pass rendering, persistent state between frames, or architectural patterns. These need direct `OperatorBase` with manual WebGPU pipeline management.

## Operators

### 1. Bloom
- **Architecture:** 3-pass pipeline (threshold extract -> separable Gaussian blur -> composite)
- **Params:** threshold (0-1, default 0.8), intensity (0-2, default 1), radius (1-32, default 8), passes (1-5, default 2)
- **Ports:** input texture, output texture
- **Implementation:**
  - Needs 2 intermediate textures (ping-pong for blur passes)
  - Pass 1: Threshold — extract pixels above brightness threshold
  - Pass 2-N: Horizontal + vertical Gaussian blur (separable, repeated `passes` times)
  - Final: Additive composite of blurred bright areas onto original
  - Must create/manage intermediate textures matching output resolution
  - Recreate intermediate textures if resolution changes
- **Complexity:** Highest — multiple render pipelines, multiple shader modules, intermediate texture management
- **Pattern:** Direct `OperatorBase` with manual pipeline setup
- **Files:** `operators/gpu/bloom/bloom.cpp` (inline WGSL shaders for all passes)

### 2. Feedback
- **Architecture:** Persistent frame buffer that feeds back into itself
- **Params:** decay (0-1, default 0.95), mix (0-1, default 0.5), offset_x (-0.5 to 0.5, default 0), offset_y (-0.5 to 0.5, default 0), zoom (0.9-1.1, default 1), rotate (0-360, default 0)
- **Ports:** input texture, output texture
- **Implementation:**
  - Maintains a persistent texture that survives across frames
  - Each frame: blend current input with transformed previous frame
  - Transform previous frame by offset/zoom/rotate before blending
  - Write result to both output and feedback buffer
  - Needs copy pass (render feedback buffer to persistent texture)
  - Must handle resolution changes (recreate persistent texture)
- **Complexity:** High — persistent state, 2 render passes per frame, texture copy
- **Pattern:** Direct `OperatorBase` with persistent texture management
- **Files:** `operators/gpu/feedback/feedback.cpp` (inline WGSL shaders)

### 3. Switch
- **Architecture:** Dynamic multi-input texture selector
- **Params:** index (int, 0-7, default 0), blend (0-1, default 0)
- **Ports:** Multiple input textures (override `collect_ports` to declare e.g. 4 inputs), output texture
- **Implementation:**
  - Select input texture by index
  - When blend > 0, crossfade between index and index+1
  - Challenge: WgslFilterBase only handles single input elegantly; multiple inputs need either custom `collect_ports` override or direct OperatorBase
  - Could potentially use WgslFilterBase with overridden `collect_ports` adding multiple GPU_TEXTURE inputs (the base class does support `inputTex0`, `inputTex1`, etc.)
- **Complexity:** Moderate architecture, simple shader
- **Pattern:** `WgslFilterBase` with overridden `collect_ports` for multiple inputs
- **Files:** `operators/gpu/switch_op/switch_op.cpp`, `operators/gpu/switch_op/switch_op.wgsl`

## CMake Additions
```cmake
add_vivid_operator(bloom      operators/gpu/bloom/bloom.cpp          EXTRA_LIBS webgpu)
add_vivid_operator(feedback   operators/gpu/feedback/feedback.cpp    EXTRA_LIBS webgpu)
add_vivid_operator(switch_op  operators/gpu/switch_op/switch_op.cpp  EXTRA_LIBS webgpu)
```

## Verification
- Bloom: chain after bright source (SolidColor or Noise), verify glow halo, adjust threshold
- Feedback: connect animated source (LFO-driven), verify trailing/echo effect with decay
- Switch: connect multiple sources, verify clean switching and crossfade blend
