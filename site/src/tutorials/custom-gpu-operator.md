## What you'll build

A custom GPU operator with a WGSL fragment shader — a generative texture that reacts to a parameter you define. You'll use Vivid's operator scaffold tool and hot-reload to iterate in real time.

## Prerequisites

You need a Vivid project directory with a local operators package. If you don't have one, the scaffold tool will create it automatically.

## Step 1: Scaffold the operator

From the Vivid build directory, run:

```bash
./build/vivid scaffold-operator --domain gpu --name MyEffect
```

This creates:
```
local-operators/
  operators/gpu/my_effect/
    my_effect.cpp
    my_effect.wgsl
    CMakeLists.txt
```

The `.cpp` file declares the operator's parameters and ports. The `.wgsl` file contains the fragment shader. Both files are minimal but complete — the operator builds and runs immediately after scaffold.

## Step 2: Understand the scaffold

Open `my_effect.cpp`. The key sections:

```cpp
struct MyEffect : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "MyEffect";

    vivid::Param<float> intensity{"intensity", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&intensity);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }
};
VIVID_REGISTER(MyEffect)
```

`collect_params` declares the parameters shown in the inspector. `collect_ports` declares input and output texture ports.

Open `my_effect.wgsl`. The scaffold generates a pass-through shader:

```wgsl
@group(0) @binding(0) var input_tex: texture_2d<f32>;
@group(0) @binding(1) var tex_sampler: sampler;
@group(0) @binding(2) var<uniform> uniforms: Uniforms;

struct Uniforms {
    intensity: f32,
}

@fragment
fn main(@builtin(position) pos: vec4f,
        @location(0) uv: vec2f) -> @location(0) vec4f {
    return textureSample(input_tex, tex_sampler, uv);
}
```

## Step 3: Build and hot-reload

Build the local-operators package:

```bash
./build/vivid build local-operators
```

The operator appears immediately in Vivid's operator browser under **Local**. Add it to the graph and connect a texture input.

From here, **edits to `.wgsl` save and hot-reload automatically** — you don't need to rebuild for shader changes. Changes to `.cpp` (new params, new ports) require a rebuild.

## Step 4: Write a custom effect

Replace the pass-through shader with a chromatic aberration effect:

```wgsl
@fragment
fn main(@builtin(position) pos: vec4f,
        @location(0) uv: vec2f) -> @location(0) vec4f {
    let amount = uniforms.intensity * 0.02;
    let r = textureSample(input_tex, tex_sampler, uv + vec2f( amount, 0.0)).r;
    let g = textureSample(input_tex, tex_sampler, uv).g;
    let b = textureSample(input_tex, tex_sampler, uv + vec2f(-amount, 0.0)).b;
    let a = textureSample(input_tex, tex_sampler, uv).a;
    return vec4f(r, g, b, a);
}
```

Save the file. The shader reloads within one frame. The `intensity` param now controls the horizontal channel split.

## Step 5: Make the param drivable from Control

The `intensity` param already accepts Control wires because all `vivid::Param<float>` fields do. Connect an **LfoFr** → `myeffect1/intensity` to animate the effect in real time.

## Adding a generative (no-input) output

To generate a texture without an input (like NoiseTexture), declare no input port and write a purely generative shader. The WGSL receives UV coordinates and frame time through the uniforms struct. Add `time: f32` to the `Uniforms` struct in both `.cpp` and `.wgsl` and use `VIVID_UNIFORM_TIME` binding to get the current timestamp.

## What's happening

The GPU operator contract:
1. `.cpp` declares params and ports — compiled into a `.dylib`
2. `.wgsl` contains the WGSL shader — compiled at startup and on hot-reload
3. Vivid passes texture bindings and a uniform buffer matching your `Uniforms` struct to the shader on each frame

The operator appears in the graph like any built-in GPU operator. Its ports are compatible with all other GPU texture ports.

## Next steps

- [Writing a Custom Audio Operator](../custom-audio-operator/) — same pattern for audio-rate DSP
- Review the operator API headers in `src/operator_api/` for advanced features (thumbnails, semantic tags, draw API)
