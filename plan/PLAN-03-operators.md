# Vivid Implementation Plan — Part 3: Operators

This document covers the operator system, built-in operators, and WGSL shaders.

---

## Operators CMakeLists.txt

Create `operators/CMakeLists.txt`:

```cmake
# Function to add an operator as a shared library
function(add_operator name)
    add_library(${name} SHARED ${name}.cpp)
    target_include_directories(${name} PRIVATE 
        ${CMAKE_SOURCE_DIR}/runtime/include
    )
    target_link_libraries(${name} PRIVATE glm::glm)
    set_target_properties(${name} PROPERTIES
        PREFIX ""  # No 'lib' prefix
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/operators
    )
    
    # Platform-specific extension
    if(WIN32)
        set_target_properties(${name} PROPERTIES SUFFIX ".dll")
    elseif(APPLE)
        set_target_properties(${name} PROPERTIES SUFFIX ".dylib")
    else()
        set_target_properties(${name} PROPERTIES SUFFIX ".so")
    endif()
endfunction()

# Built-in operators
add_operator(noise)
add_operator(lfo)
add_operator(feedback)
add_operator(composite)
add_operator(brightness)
add_operator(blur)
add_operator(hsv)
add_operator(transform)
```

---

## Built-in Operators

### operators/noise.cpp

```cpp
#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

struct NoiseState : OperatorState {
    float phase = 0.0f;
};

class Noise : public Operator {
public:
    // Fluent API for parameter setting
    Noise& scale(float s) { scale_ = s; return *this; }
    Noise& speed(float s) { speed_ = s; return *this; }
    Noise& octaves(int o) { octaves_ = o; return *this; }
    Noise& lacunarity(float l) { lacunarity_ = l; return *this; }
    Noise& persistence(float p) { persistence_ = p; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        phase_ += ctx.dt() * speed_;
        
        ctx.runShader("noise", {
            {"u_scale", scale_},
            {"u_phase", phase_},
            {"u_octaves", octaves_},
            {"u_lacunarity", lacunarity_},
            {"u_persistence", persistence_},
            {"u_resolution", glm::vec2(ctx.width(), ctx.height())},
            {"u_time", ctx.time()}
        }, output_);
        
        ctx.setOutput("out", output_);
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<NoiseState>();
        state->phase = phase_;
        return state;
    }

    void loadState(std::unique_ptr<OperatorState> state) override {
        if (auto* s = dynamic_cast<NoiseState*>(state.get())) {
            phase_ = s->phase;
        }
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("scale", scale_, 0.1f, 50.0f),
            floatParam("speed", speed_, 0.0f, 10.0f),
            intParam("octaves", octaves_, 1, 8),
            floatParam("lacunarity", lacunarity_, 1.0f, 4.0f),
            floatParam("persistence", persistence_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    float scale_ = 4.0f;
    float speed_ = 1.0f;
    int octaves_ = 4;
    float lacunarity_ = 2.0f;
    float persistence_ = 0.5f;
    float phase_ = 0.0f;
    Texture output_;
};

VIVID_OPERATOR(Noise)
```

### operators/lfo.cpp

```cpp
#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

class LFO : public Operator {
public:
    explicit LFO(float freq = 1.0f) : freq_(freq) {}
    
    LFO& freq(float f) { freq_ = f; return *this; }
    LFO& min(float m) { min_ = m; return *this; }
    LFO& max(float m) { max_ = m; return *this; }
    LFO& phase(float p) { phaseOffset_ = p; return *this; }
    LFO& waveform(int w) { waveform_ = w; return *this; }  // 0=sine, 1=saw, 2=square, 3=triangle

    void process(Context& ctx) override {
        float t = ctx.time() * freq_ + phaseOffset_;
        float normalized = 0.0f;
        
        switch (waveform_) {
            case 0:  // Sine
                normalized = (std::sin(t * 2.0f * 3.14159f) + 1.0f) * 0.5f;
                break;
            case 1:  // Saw
                normalized = std::fmod(t, 1.0f);
                break;
            case 2:  // Square
                normalized = std::fmod(t, 1.0f) < 0.5f ? 0.0f : 1.0f;
                break;
            case 3:  // Triangle
                normalized = std::abs(std::fmod(t * 2.0f, 2.0f) - 1.0f);
                break;
        }
        
        value_ = min_ + normalized * (max_ - min_);
        ctx.setOutput("out", value_);
        
        // Also store recent history for visualization
        history_.push_back(value_);
        if (history_.size() > 64) {
            history_.erase(history_.begin());
        }
        ctx.setOutput("history", history_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("freq", freq_, 0.01f, 100.0f),
            floatParam("min", min_, -1000.0f, 1000.0f),
            floatParam("max", max_, -1000.0f, 1000.0f),
            floatParam("phase", phaseOffset_, 0.0f, 1.0f),
            intParam("waveform", waveform_, 0, 3)
        };
    }

    OutputKind outputKind() override { return OutputKind::Value; }

private:
    float freq_ = 1.0f;
    float min_ = 0.0f;
    float max_ = 1.0f;
    float phaseOffset_ = 0.0f;
    int waveform_ = 0;
    float value_ = 0.0f;
    std::vector<float> history_;
};

VIVID_OPERATOR(LFO)
```

### operators/feedback.cpp

```cpp
#include <vivid/vivid.h>

using namespace vivid;

struct FeedbackState : OperatorState {
    bool initialized = false;
};

class Feedback : public Operator {
public:
    Feedback() = default;
    explicit Feedback(const std::string& inputNode) : inputNode_(inputNode) {}
    
    Feedback& input(const std::string& node) { inputNode_ = node; return *this; }
    Feedback& decay(float d) { decay_ = d; return *this; }
    Feedback& zoom(float z) { zoom_ = z; return *this; }
    Feedback& rotate(float r) { rotate_ = r; return *this; }
    Feedback& translate(glm::vec2 t) { translate_ = t; return *this; }

    void init(Context& ctx) override {
        buffer_[0] = ctx.createTexture();
        buffer_[1] = ctx.createTexture();
        currentBuffer_ = 0;
    }

    void process(Context& ctx) override {
        Texture input = ctx.getInputTexture(inputNode_);
        Texture& current = buffer_[currentBuffer_];
        Texture& previous = buffer_[1 - currentBuffer_];
        
        ctx.runShader("feedback", {
            {"u_decay", decay_},
            {"u_zoom", zoom_},
            {"u_rotate", rotate_},
            {"u_translate", translate_},
            {"u_resolution", glm::vec2(ctx.width(), ctx.height())}
        }, {input, previous}, current);
        
        ctx.setOutput("out", current);
        
        // Swap buffers
        currentBuffer_ = 1 - currentBuffer_;
    }

    std::unique_ptr<OperatorState> saveState() override {
        auto state = std::make_unique<FeedbackState>();
        state->initialized = true;
        return state;
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("decay", decay_, 0.0f, 1.0f),
            floatParam("zoom", zoom_, 0.9f, 1.1f),
            floatParam("rotate", rotate_, -0.1f, 0.1f),
            vec2Param("translate", translate_)
        };
    }

private:
    std::string inputNode_;
    float decay_ = 0.95f;
    float zoom_ = 1.0f;
    float rotate_ = 0.0f;
    glm::vec2 translate_ = glm::vec2(0.0f);
    Texture buffer_[2];
    int currentBuffer_ = 0;
};

VIVID_OPERATOR(Feedback)
```

### operators/composite.cpp

```cpp
#include <vivid/vivid.h>

using namespace vivid;

class Composite : public Operator {
public:
    Composite() = default;
    
    Composite& a(const std::string& node) { nodeA_ = node; return *this; }
    Composite& b(const std::string& node) { nodeB_ = node; return *this; }
    Composite& mode(int m) { mode_ = m; return *this; }  // 0=over, 1=add, 2=multiply, 3=screen, 4=difference
    Composite& mix(float m) { mix_ = m; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture texA = ctx.getInputTexture(nodeA_);
        Texture texB = ctx.getInputTexture(nodeB_);
        
        ctx.runShader("composite", {
            {"u_mode", mode_},
            {"u_mix", mix_}
        }, {texA, texB}, output_);
        
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("mode", mode_, 0, 4),
            floatParam("mix", mix_, 0.0f, 1.0f)
        };
    }

private:
    std::string nodeA_;
    std::string nodeB_;
    int mode_ = 0;
    float mix_ = 0.5f;
    Texture output_;
};

VIVID_OPERATOR(Composite)
```

### operators/brightness.cpp

```cpp
#include <vivid/vivid.h>

using namespace vivid;

class Brightness : public Operator {
public:
    Brightness() = default;
    explicit Brightness(const std::string& inputNode) : inputNode_(inputNode) {}
    
    Brightness& input(const std::string& node) { inputNode_ = node; return *this; }
    Brightness& amount(float a) { amount_ = a; return *this; }
    Brightness& amount(const std::string& node) { amountNode_ = node; useNode_ = true; return *this; }
    Brightness& contrast(float c) { contrast_ = c; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture input = ctx.getInputTexture(inputNode_);
        
        float finalAmount = amount_;
        if (useNode_ && !amountNode_.empty()) {
            finalAmount = ctx.getInputValue(amountNode_);
        }
        
        ctx.runShader("brightness", {
            {"u_brightness", finalAmount},
            {"u_contrast", contrast_}
        }, {input}, output_);
        
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("amount", amount_, -1.0f, 2.0f),
            floatParam("contrast", contrast_, 0.0f, 3.0f)
        };
    }

private:
    std::string inputNode_;
    std::string amountNode_;
    bool useNode_ = false;
    float amount_ = 1.0f;
    float contrast_ = 1.0f;
    Texture output_;
};

VIVID_OPERATOR(Brightness)
```

### operators/hsv.cpp

```cpp
#include <vivid/vivid.h>

using namespace vivid;

class HSVAdjust : public Operator {
public:
    HSVAdjust() = default;
    explicit HSVAdjust(const std::string& inputNode) : inputNode_(inputNode) {}
    
    HSVAdjust& input(const std::string& node) { inputNode_ = node; return *this; }
    HSVAdjust& hueShift(float h) { hueShift_ = h; return *this; }
    HSVAdjust& saturation(float s) { saturation_ = s; return *this; }
    HSVAdjust& value(float v) { value_ = v; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture input = ctx.getInputTexture(inputNode_);
        
        ctx.runShader("hsv", {
            {"u_hueShift", hueShift_},
            {"u_saturation", saturation_},
            {"u_value", value_}
        }, {input}, output_);
        
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("hueShift", hueShift_, -1.0f, 1.0f),
            floatParam("saturation", saturation_, 0.0f, 3.0f),
            floatParam("value", value_, 0.0f, 3.0f)
        };
    }

private:
    std::string inputNode_;
    float hueShift_ = 0.0f;
    float saturation_ = 1.0f;
    float value_ = 1.0f;
    Texture output_;
};

VIVID_OPERATOR(HSVAdjust)
```

### operators/blur.cpp

```cpp
#include <vivid/vivid.h>

using namespace vivid;

class Blur : public Operator {
public:
    Blur() = default;
    explicit Blur(const std::string& inputNode) : inputNode_(inputNode) {}
    
    Blur& input(const std::string& node) { inputNode_ = node; return *this; }
    Blur& radius(float r) { radius_ = r; return *this; }
    Blur& passes(int p) { passes_ = p; return *this; }

    void init(Context& ctx) override {
        temp_ = ctx.createTexture();
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture input = ctx.getInputTexture(inputNode_);
        
        // Two-pass separable blur
        for (int i = 0; i < passes_; i++) {
            Texture& src = (i == 0) ? input : output_;
            
            // Horizontal pass
            ctx.runShader("blur", {
                {"u_direction", glm::vec2(1.0f, 0.0f)},
                {"u_radius", radius_},
                {"u_resolution", glm::vec2(ctx.width(), ctx.height())}
            }, {src}, temp_);
            
            // Vertical pass
            ctx.runShader("blur", {
                {"u_direction", glm::vec2(0.0f, 1.0f)},
                {"u_radius", radius_},
                {"u_resolution", glm::vec2(ctx.width(), ctx.height())}
            }, {temp_}, output_);
        }
        
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            floatParam("radius", radius_, 0.0f, 50.0f),
            intParam("passes", passes_, 1, 5)
        };
    }

private:
    std::string inputNode_;
    float radius_ = 5.0f;
    int passes_ = 1;
    Texture temp_;
    Texture output_;
};

VIVID_OPERATOR(Blur)
```

### operators/transform.cpp

```cpp
#include <vivid/vivid.h>

using namespace vivid;

class Transform : public Operator {
public:
    Transform() = default;
    explicit Transform(const std::string& inputNode) : inputNode_(inputNode) {}
    
    Transform& input(const std::string& node) { inputNode_ = node; return *this; }
    Transform& translate(glm::vec2 t) { translate_ = t; return *this; }
    Transform& scale(glm::vec2 s) { scale_ = s; return *this; }
    Transform& scale(float s) { scale_ = glm::vec2(s); return *this; }
    Transform& rotate(float r) { rotate_ = r; return *this; }
    Transform& pivot(glm::vec2 p) { pivot_ = p; return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
    }

    void process(Context& ctx) override {
        Texture input = ctx.getInputTexture(inputNode_);
        
        ctx.runShader("transform", {
            {"u_translate", translate_},
            {"u_scale", scale_},
            {"u_rotate", rotate_},
            {"u_pivot", pivot_}
        }, {input}, output_);
        
        ctx.setOutput("out", output_);
    }

    std::vector<ParamDecl> params() override {
        return {
            vec2Param("translate", translate_),
            vec2Param("scale", scale_),
            floatParam("rotate", rotate_, -3.14159f, 3.14159f),
            vec2Param("pivot", pivot_)
        };
    }

private:
    std::string inputNode_;
    glm::vec2 translate_ = glm::vec2(0.0f);
    glm::vec2 scale_ = glm::vec2(1.0f);
    float rotate_ = 0.0f;
    glm::vec2 pivot_ = glm::vec2(0.5f);
    Texture output_;
};

VIVID_OPERATOR(Transform)
```

---

## WGSL Shaders

### shaders/fullscreen.wgsl

Common fullscreen triangle vertex shader (included in other shaders):

```wgsl
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    // Fullscreen triangle
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;  // Flip Y
    return out;
}
```

### shaders/noise.wgsl

```wgsl
struct Uniforms {
    scale: f32,
    phase: f32,
    octaves: i32,
    lacunarity: f32,
    persistence: f32,
    time: f32,
    resolution: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

// Permutation polynomial
fn permute3(x: vec3f) -> vec3f {
    return (((x * 34.0) + 1.0) * x) % 289.0;
}

// Simplex 2D noise
fn snoise2(v: vec2f) -> f32 {
    let C = vec4f(0.211324865405187, 0.366025403784439,
                  -0.577350269189626, 0.024390243902439);
    
    var i = floor(v + dot(v, C.yy));
    let x0 = v - i + dot(i, C.xx);
    
    var i1: vec2f;
    if (x0.x > x0.y) {
        i1 = vec2f(1.0, 0.0);
    } else {
        i1 = vec2f(0.0, 1.0);
    }
    
    var x12 = x0.xyxy + C.xxzz;
    x12 = vec4f(x12.xy - i1, x12.zw);
    
    i = i % 289.0;
    let p = permute3(permute3(i.y + vec3f(0.0, i1.y, 1.0)) + i.x + vec3f(0.0, i1.x, 1.0));
    
    var m = max(0.5 - vec3f(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), vec3f(0.0));
    m = m * m;
    m = m * m;
    
    let x = 2.0 * fract(p * C.www) - 1.0;
    let h = abs(x) - 0.5;
    let ox = floor(x + 0.5);
    let a0 = x - ox;
    
    m = m * (1.79284291400159 - 0.85373472095314 * (a0 * a0 + h * h));
    
    let g = vec3f(
        a0.x * x0.x + h.x * x0.y,
        a0.y * x12.x + h.y * x12.y,
        a0.z * x12.z + h.z * x12.w
    );
    
    return 130.0 * dot(m, g);
}

// Fractal Brownian Motion
fn fbm(p: vec2f, octaves: i32, lacunarity: f32, persistence: f32) -> f32 {
    var value = 0.0;
    var amplitude = 0.5;
    var frequency = 1.0;
    var pos = p;
    
    for (var i = 0; i < octaves; i++) {
        value += amplitude * snoise2(pos * frequency);
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    
    return value;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let uv = in.uv * u.scale;
    let offset = vec2f(u.phase * 0.5, u.phase * 0.3);
    
    let n = fbm(uv + offset, u.octaves, u.lacunarity, u.persistence);
    let value = n * 0.5 + 0.5;  // Normalize to 0-1
    
    return vec4f(value, value, value, 1.0);
}
```

### shaders/feedback.wgsl

```wgsl
struct Uniforms {
    decay: f32,
    zoom: f32,
    rotate: f32,
    translate: vec2f,
    resolution: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;
@group(0) @binding(3) var feedbackTex: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Transform UV for feedback sampling
    var uv = in.uv - 0.5;  // Center
    
    // Apply zoom
    uv = uv / u.zoom;
    
    // Apply rotation
    let c = cos(u.rotate);
    let s = sin(u.rotate);
    uv = vec2f(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
    
    // Apply translation
    uv = uv + u.translate;
    
    uv = uv + 0.5;  // Uncenter
    
    // Sample feedback with decay
    let feedback = textureSample(feedbackTex, texSampler, uv) * u.decay;
    
    // Sample new input
    let input = textureSample(inputTex, texSampler, in.uv);
    
    // Composite: new input over decayed feedback
    return max(input, feedback);
}
```

### shaders/composite.wgsl

```wgsl
struct Uniforms {
    mode: i32,
    mix_amount: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var texA: texture_2d<f32>;
@group(0) @binding(3) var texB: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let a = textureSample(texA, texSampler, in.uv);
    let b = textureSample(texB, texSampler, in.uv);
    
    var result: vec4f;
    
    switch (u.mode) {
        case 0: {  // Over (lerp)
            result = mix(a, b, u.mix_amount);
        }
        case 1: {  // Add
            result = a + b * u.mix_amount;
        }
        case 2: {  // Multiply
            result = mix(a, a * b, u.mix_amount);
        }
        case 3: {  // Screen
            result = mix(a, 1.0 - (1.0 - a) * (1.0 - b), u.mix_amount);
        }
        case 4: {  // Difference
            result = mix(a, abs(a - b), u.mix_amount);
        }
        default: {
            result = a;
        }
    }
    
    return vec4f(result.rgb, 1.0);
}
```

### shaders/brightness.wgsl

```wgsl
struct Uniforms {
    brightness: f32,
    contrast: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var color = textureSample(inputTex, texSampler, in.uv).rgb;
    
    // Apply contrast (around 0.5 gray point)
    color = (color - 0.5) * u.contrast + 0.5;
    
    // Apply brightness
    color = color * u.brightness;
    
    return vec4f(clamp(color, vec3f(0.0), vec3f(1.0)), 1.0);
}
```

### shaders/hsv.wgsl

```wgsl
struct Uniforms {
    hueShift: f32,
    saturation: f32,
    value: f32,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

fn rgb2hsv(c: vec3f) -> vec3f {
    let K = vec4f(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    let p = mix(vec4f(c.bg, K.wz), vec4f(c.gb, K.xy), step(c.b, c.g));
    let q = mix(vec4f(p.xyw, c.r), vec4f(c.r, p.yzx), step(p.x, c.r));
    
    let d = q.x - min(q.w, q.y);
    let e = 1.0e-10;
    return vec3f(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

fn hsv2rgb(c: vec3f) -> vec3f {
    let K = vec4f(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    let p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, vec3f(0.0), vec3f(1.0)), c.y);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, in.uv).rgb;
    
    var hsv = rgb2hsv(color);
    hsv.x = fract(hsv.x + u.hueShift);
    hsv.y = hsv.y * u.saturation;
    hsv.z = hsv.z * u.value;
    
    let rgb = hsv2rgb(hsv);
    return vec4f(clamp(rgb, vec3f(0.0), vec3f(1.0)), 1.0);
}
```

### shaders/blur.wgsl

```wgsl
struct Uniforms {
    direction: vec2f,
    radius: f32,
    resolution: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let pixelSize = 1.0 / u.resolution;
    let dir = u.direction * pixelSize;
    
    var color = vec4f(0.0);
    var totalWeight = 0.0;
    
    let samples = i32(u.radius * 2.0) + 1;
    let halfSamples = f32(samples) / 2.0;
    
    for (var i = 0; i < samples; i++) {
        let offset = (f32(i) - halfSamples) * dir * u.radius / halfSamples;
        
        // Gaussian weight
        let dist = length(offset * u.resolution);
        let weight = exp(-dist * dist / (2.0 * u.radius * u.radius));
        
        color += textureSample(inputTex, texSampler, in.uv + offset) * weight;
        totalWeight += weight;
    }
    
    return color / totalWeight;
}
```

### shaders/transform.wgsl

```wgsl
struct Uniforms {
    translate: vec2f,
    scale: vec2f,
    rotate: f32,
    pivot: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTex: texture_2d<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    out.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    out.uv = out.position.xy * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Transform UV (inverse transform to sample source)
    var uv = in.uv;
    
    // Move to pivot
    uv = uv - u.pivot;
    
    // Inverse scale
    uv = uv / u.scale;
    
    // Inverse rotation
    let c = cos(-u.rotate);
    let s = sin(-u.rotate);
    uv = vec2f(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
    
    // Inverse translation
    uv = uv - u.translate;
    
    // Move back from pivot
    uv = uv + u.pivot;
    
    // Sample with border handling
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }
    
    return textureSample(inputTex, texSampler, uv);
}
```

---

## Example Project

### examples/hello/chain.cpp

```cpp
#include <vivid/vivid.h>

void setup(vivid::Context& ctx) {
    // Generate noise texture
    auto noise = NODE(Noise())
        .scale(4.0)
        .speed(0.5)
        .octaves(4);
    
    // Oscillating brightness
    auto lfo = NODE(LFO(0.5))
        .min(0.5)
        .max(1.5);
    
    // Apply brightness modulation
    auto bright = NODE(Brightness("noise"))
        .amount("lfo");
    
    // Feedback for trails
    auto fb = NODE(Feedback("bright"))
        .decay(0.95)
        .zoom(1.01)
        .rotate(0.01);
    
    // Color shift
    auto final = NODE(HSVAdjust("fb"))
        .hueShift(ctx.time() * 0.1)
        .saturation(1.2);
    
    Output(final);
}
```

---

## Core vs Addon Decision Guidelines

When deciding whether a feature belongs in core or an addon, use these criteria:

### Put in CORE when:
- **Universally needed** - Most projects will use it (video, images, basic effects)
- **Lightweight** - No heavy external dependencies
- **Shader-based** - GPU-accelerated effects with minimal C++ code
- **Fundamental** - Math, logic, generators, basic compositing

### Put in ADDON when:
- **Heavy dependencies** - Adds significant binary size or build time
  - Example: Assimp adds ~15 MB and 5-10 min build time
- **Specialized** - Only a subset of projects need it
- **Optional** - Nice to have but not essential for basic visual output

### Current Division

| Core (~15 MB, 25 operators) | Addons (opt-in) |
|----------------------------|-----------------|
| VideoFile, Webcam, ImageFile, AudioIn | vivid-models (Assimp) |
| Noise, Gradient, Shape, Constant | vivid-storage (JSON persistence) |
| LFO, Math, Logic | vivid-imgui (pending) |
| All shader-based effects (Blur, Composite, etc.) | |

**Key principle:** Dependency weight is the primary criterion, not frequency of use. A user who never loads 3D models shouldn't pay the 15 MB + 5 min build cost.

---

## Addons

Vivid supports operator-level addons that extend functionality beyond the core runtime. Addons are detected automatically based on `#include` patterns in user code.

> **See [PLAN-08-addons.md](PLAN-08-addons.md) for full addon system documentation.**

### Current Addons

| Addon | Platform | Status | Description |
|-------|----------|--------|-------------|
| vivid-spout | Windows | ✅ Working | Spout texture sharing |
| vivid-syphon | macOS | ✅ Working | Syphon texture sharing |
| vivid-models | All | ✅ Working | 3D model loading (Assimp) |
| vivid-storage | All | ✅ Working | JSON key/value storage |
| vivid-nuklear | All | ✅ Working | Nuklear GUI integration |
| vivid-csg | All | ✅ Working | CSG boolean operations |
| vivid-imgui | All | ⏳ Blocked | ImGUI (WebGPU API mismatch, see PLAN-09) |

---

## Next Part

- **PLAN-04-extension.md** — VS Code extension, WebSocket protocol, inline decorations
