# Vivid Operator Roadmap

Operators to add based on TouchDesigner comparison. Organized by priority with implementation notes.

---

## Phase 1: Core 2D Operators (Quick Wins)

Simple texture operators that fill common gaps.

### Threshold
- [x] **Threshold** - Binary thresholding
  - **TD equivalent:** Threshold TOP
  - **Description:** Convert grayscale to black/white at adjustable threshold level
  - **Params:** `threshold` (0-1), `softness` (edge blend), `invert`
  - **Base class:** `SimpleTextureEffect<Threshold>`
  - **Shader:** Single-pass, compare luminance to threshold
  - **Difficulty:** Easy (1-2 hours)

### Level
- [ ] **Level** - Input/output level adjustment
  - **TD equivalent:** Level TOP
  - **Description:** Full color correction with input range, gamma, output range
  - **Params:** `inBlack`, `inWhite`, `gamma`, `outBlack`, `outWhite` (per-channel or master)
  - **Base class:** `SimpleTextureEffect<Level>`
  - **Shader:** `out = pow((in - inBlack) / (inWhite - inBlack), gamma) * (outWhite - outBlack) + outBlack`
  - **Difficulty:** Easy (2-3 hours)
  - **Note:** More powerful than existing Brightness operator

### Lookup / LUT
- [ ] **Lookup** - Color lookup table
  - **TD equivalent:** Lookup TOP
  - **Description:** Remap colors using 1D gradient or 2D LUT texture
  - **Params:** `lutInput` (texture), `mode` (1D/2D/3D), `intensity`
  - **Base class:** `SimpleTextureEffect<Lookup>`
  - **Shader:** Sample LUT texture using input color channels as UV
  - **Difficulty:** Medium (3-4 hours)
  - **Formats:** Support .cube LUT files for color grading

### Crop
- [ ] **Crop** - Region extraction
  - **TD equivalent:** Crop TOP
  - **Description:** Extract rectangular region from texture
  - **Params:** `left`, `right`, `top`, `bottom` (pixels or normalized)
  - **Base class:** `SimpleTextureEffect<Crop>`
  - **Shader:** UV remapping to crop region
  - **Difficulty:** Easy (1-2 hours)

### Fit
- [ ] **Fit** - Resolution fitting
  - **TD equivalent:** Fit TOP
  - **Description:** Fit texture to target resolution with letterbox/pillarbox
  - **Params:** `width`, `height`, `fitMode` (Fill, Fit, Stretch, Native), `justify` (center/top/bottom/left/right)
  - **Base class:** `TextureOperator` (needs custom resolution)
  - **Shader:** UV transform with aspect ratio handling
  - **Difficulty:** Medium (2-3 hours)

### Text
- [ ] **Text** - Dedicated text rendering
  - **TD equivalent:** Text TOP
  - **Description:** Render text to texture with font control
  - **Params:** `text`, `font`, `fontSize`, `color`, `align`, `wordWrap`, `backgroundColor`
  - **Base class:** `TextureOperator`
  - **Implementation:** Use existing FreeType integration from Canvas
  - **Difficulty:** Medium (4-6 hours) - factor out text from Canvas
  - **Note:** Canvas has text but dedicated operator is cleaner

---

## Phase 2: Motion & Compositing

Operators for motion effects and advanced compositing.

### Normal Map
- [ ] **NormalMap** - Height to normal conversion
  - **TD equivalent:** Normal Map TOP
  - **Description:** Generate normal map from height/displacement texture
  - **Params:** `strength`, `flipY`, `format` (tangent/object space)
  - **Base class:** `SimpleTextureEffect<NormalMap>`
  - **Shader:** Sobel-based gradient to normal conversion
  - **Difficulty:** Easy (2-3 hours)

### Layout
- [ ] **Layout** - Multi-texture arrangement
  - **TD equivalent:** Layout TOP
  - **Description:** Arrange multiple textures in grid or custom positions
  - **Params:** `columns`, `rows`, `spacing`, `justify`
  - **Inputs:** Multiple texture inputs
  - **Base class:** `TextureOperator` (multi-input)
  - **Difficulty:** Medium (4-6 hours)

### Corner Pin
- [ ] **CornerPin** - 4-point perspective warp
  - **TD equivalent:** Corner Pin TOP
  - **Description:** Warp texture using 4 corner positions
  - **Params:** `topLeft`, `topRight`, `bottomLeft`, `bottomRight` (vec2 each)
  - **Shader:** Perspective transform matrix from corners
  - **Difficulty:** Medium (3-4 hours)
  - **Uses:** Projection mapping, AR overlays

---

## Phase 3: Control & Animation

Channel operators for parameter control and animation.

### Lag
- [ ] **Lag** - Value smoothing
  - **TD equivalent:** Lag CHOP
  - **Description:** Smooth/filter parameter values over time
  - **Params:** `lagUp`, `lagDown`, `overshoot`
  - **Implementation:** Exponential smoothing or spring physics
  - **Difficulty:** Easy (2-3 hours)
  - **Note:** Could be a Param modifier rather than operator

### Timer
- [ ] **Timer** - Programmable timer
  - **TD equivalent:** Timer CHOP
  - **Description:** Configurable timer with segments, loops, callbacks
  - **Params:** `duration`, `loop`, `segments[]`, `playing`
  - **Outputs:** `fraction`, `seconds`, `segment`, `done`
  - **Difficulty:** Medium (3-4 hours)

### Expression
- [ ] **Expression** - Math expression evaluator
  - **TD equivalent:** Expression CHOP
  - **Description:** Evaluate math expressions for parameter values
  - **Params:** `expression` (string), variable bindings
  - **Implementation:** Simple expression parser or use exprtk library
  - **Difficulty:** Hard (6-10 hours)
  - **Alternative:** Enhance existing MathOp

---

## Phase 4: 3D Enhancement

Geometry operators and 3D post-processing.

### Extrude
- [ ] **Extrude** - 2D to 3D extrusion
  - **TD equivalent:** Extrude SOP
  - **Description:** Extrude 2D shapes into 3D geometry
  - **Params:** `depth`, `divisions`, `caps` (front/back/both)
  - **Input:** 2D path/shape
  - **Difficulty:** Hard (8-12 hours)
  - **Dependency:** Need 2D path/curve representation

### Sweep
- [ ] **Sweep** - Profile along path
  - **TD equivalent:** Sweep SOP
  - **Description:** Sweep cross-section profile along a path
  - **Params:** `profile`, `path`, `twist`, `scale`
  - **Difficulty:** Hard (8-12 hours)
  - **Dependency:** Need curve/path operators

### Trail
- [ ] **Trail** - Motion trails
  - **TD equivalent:** Trail SOP
  - **Description:** Create geometry trails from moving points
  - **Params:** `length`, `fadeout`, `width`
  - **Difficulty:** Medium (4-6 hours)
  - **Integration:** Could work with particle system

### LSystem
- [ ] **LSystem** - L-system fractals
  - **TD equivalent:** L-System SOP
  - **Description:** Generate fractal geometry from L-system rules
  - **Params:** `axiom`, `rules[]`, `iterations`, `angle`, `length`
  - **Difficulty:** Medium (6-8 hours)
  - **Output:** Line geometry or instanced branches

### SSAO
- [ ] **SSAO** - Screen-space ambient occlusion
  - **TD equivalent:** SSAO TOP
  - **Description:** Add ambient occlusion in screen space
  - **Params:** `radius`, `intensity`, `bias`, `samples`
  - **Input:** Color + depth from Render3D
  - **Difficulty:** Medium (4-6 hours)
  - **Note:** Render3D already outputs depth

### ToneMap
- [ ] **ToneMap** - HDR to SDR
  - **TD equivalent:** Tone Map TOP
  - **Description:** Map HDR values to displayable range
  - **Params:** `exposure`, `mode` (Reinhard/ACES/Filmic), `whitePoint`
  - **Base class:** `SimpleTextureEffect<ToneMap>`
  - **Difficulty:** Easy (2-3 hours)

---

## Phase 5: Interoperability

External integration for professional workflows.

### NDI
- [ ] **NDIIn** / **NDIOut** - Network video
  - **TD equivalent:** NDI In/Out TOP
  - **Description:** Send/receive video over network via NDI protocol
  - **Dependency:** NDI SDK (free, requires registration)
  - **Difficulty:** Medium (6-8 hours per direction)
  - **Uses:** Broadcast, multi-machine setups

### Syphon/Spout
- [ ] **SyphonSpoutIn** / **SyphonSpoutOut** - App texture sharing
  - **TD equivalent:** Syphon Spout In/Out TOP
  - **Description:** Share textures between applications
  - **Platform:** Syphon (macOS), Spout (Windows)
  - **Difficulty:** Medium (4-6 hours per platform)
  - **Uses:** VJ software integration, multi-app workflows

### Ableton Link
- [ ] **AbletonLink** - Music sync
  - **TD equivalent:** Ableton Link CHOP
  - **Description:** Sync tempo/phase with Ableton and other Link-enabled apps
  - **Dependency:** Link SDK (open source)
  - **Outputs:** `bpm`, `beat`, `phase`, `playing`
  - **Difficulty:** Medium (4-6 hours)

---

## Lower Priority (Tier 2-3)

### 2D Effects
- [ ] **Emboss** - Emboss/deboss effect
- [ ] **Function** - Per-pixel math (pow, sin, abs, etc.)
- [ ] **Analyze** - Texture statistics (min/max/avg)
- [ ] **AntiAlias** - Post-process anti-aliasing (FXAA/SMAA)
- [ ] **SVG** - Render SVG graphics

### Geometry
- [ ] **Twist** - Twist deformation
- [ ] **Bend** - Bend deformation
- [ ] **Subdivide** - Catmull-Clark subdivision
- [ ] **Carve** - Trim/carve curves
- [ ] **Sprinkle** - Scatter points on surface
- [ ] **Revolve** - Revolve profile around axis
- [ ] **Metaball** - Metaball/blobby surfaces

### Audio
- [ ] **AudioVST** - VST plugin hosting (complex licensing)
- [ ] **LTCIn/Out** - Timecode sync

### Hardware
- [ ] **Kinect** - Kinect depth camera
- [ ] **RealSense** - Intel RealSense
- [ ] **LeapMotion** - Hand tracking

---

## Planned in Optional Modules

These operators belong in optional modules rather than core:

| Operator | Module | TD Equivalent | Notes |
|----------|--------|---------------|-------|
| OpticalFlow | vivid-opencv | Optical Flow TOP | Motion vectors via Horn-Schunck/Lucas-Kanade |
| FaceDetect | vivid-opencv | - | Haar cascade or DNN face detection |
| Contours | vivid-opencv | - | Edge/shape detection |
| ColorTrack | vivid-opencv | - | Color-based object tracking |
| BlobTrack | vivid-opencv | Blob Track TOP | Blob detection and tracking |

---

## Already Covered (No Action Needed)

These TouchDesigner operators have Vivid equivalents:

| TD Operator | Vivid Equivalent |
|-------------|------------------|
| Constant TOP | SolidColor |
| Noise TOP | Noise |
| Ramp TOP | Gradient, Ramp |
| Circle/Rectangle TOP | Shape |
| Blur TOP | Blur |
| Composite TOP | Composite |
| Displace TOP | Displace |
| Mirror TOP | Mirror |
| Tile TOP | Tile |
| HSV Adjust TOP | HSV |
| Edge TOP | Edge |
| Feedback TOP | Feedback |
| Cache TOP | FrameCache |
| Time Machine TOP | TimeMachine |
| Transform TOP | Transform |
| Bloom TOP | Bloom |
| Depth TOP | Render3D depth output |
| Movie File In TOP | VideoPlayer |
| Video Device In TOP | Webcam |
| Audio Device In CHOP | AudioIn |
| Audio Filter CHOP | AudioFilter |
| LFO CHOP | LFO |
| Noise CHOP | Noise (audio) |
| Clock CHOP | Clock |
| OSC In/Out CHOP | OSCIn/OSCOut |
| MIDI In/Out CHOP | MIDIIn/MIDIOut |
| DMX Out CHOP | DMXOut |

---

## Implementation Notes

### Shader Template (SimpleTextureEffect)
```cpp
class MyEffect : public SimpleTextureEffect<MyEffect> {
public:
    Param<float> amount{"amount", 1.0f, 0.0f, 2.0f};

    std::string fragmentShader() const override {
        return R"(
            @group(0) @binding(0) var inputTex: texture_2d<f32>;
            @group(0) @binding(1) var inputSampler: sampler;

            struct Uniforms { amount: f32 }
            @group(1) @binding(0) var<uniform> u: Uniforms;

            @fragment
            fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> {
                let color = textureSample(inputTex, inputSampler, uv);
                // Effect logic here
                return color;
            }
        )";
    }
};
```

### Adding to Registry
```cpp
// In operator_registrations.cpp
VIVID_REGISTER_OPERATOR(MyEffect, "myeffect", "2D Effects");
```

### Estimated Total Effort

| Phase | Operators | Est. Hours |
|-------|-----------|------------|
| Phase 1 | 6 | 15-20 |
| Phase 2 | 3 | 9-13 |
| Phase 3 | 3 | 12-17 |
| Phase 4 | 6 | 35-50 |
| Phase 5 | 3 | 15-20 |
| **Total** | **21** | **86-120** |

*Note: OpticalFlow and other vision operators moved to vivid-opencv module.*
