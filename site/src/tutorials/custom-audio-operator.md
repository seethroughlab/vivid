## What you'll build

A custom audio-rate DSP operator in C++ — a simple ring modulator (amplitude modulation) with a carrier frequency parameter. You'll scaffold it, write the DSP loop, and hot-reload it into a running graph.

## Prerequisites

Same as [Writing a Custom GPU Operator](../custom-gpu-operator/) — you need a Vivid project directory.

## Step 1: Scaffold the operator

```bash
./build/vivid scaffold-operator --domain audio --name RingMod2
```

This creates:
```
local-operators/
  operators/audio/ring_mod_2/
    ring_mod_2.cpp
    CMakeLists.txt
```

Audio operators have no shader file — all processing is C++.

## Step 2: Understand the scaffold

Open `ring_mod_2.cpp`:

```cpp
struct RingMod2 : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "RingMod2";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> carrier_freq{"carrier_freq", 200.0f, 20.0f, 2000.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&carrier_freq);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        // DSP loop goes here
    }
};
VIVID_REGISTER(RingMod2)
```

`kTimeDependent = true` tells the compiler this operator's output changes over time even with fixed inputs.

## Step 3: Write the DSP loop

Ring modulation multiplies the input signal by a carrier sine wave. Replace the empty `process_audio`:

```cpp
void process_audio(const VividAudioContext* ctx) override {
    const float* in  = ctx->input_buffers[0];
    float*       out = ctx->output_buffers[0];
    const int    n   = ctx->block_size;
    const float  sr  = static_cast<float>(ctx->sample_rate);

    float freq = carrier_freq.value;
    float phase_inc = freq / sr;

    for (int i = 0; i < n; ++i) {
        phase_ = std::fmod(phase_ + phase_inc, 1.0f);
        float carrier = std::sin(phase_ * 6.283185f);
        out[i] = in[i] * carrier;
    }
}

private:
    float phase_ = 0.0f;
```

Add `#include <cmath>` at the top if it's not already there.

## Step 4: Build and test

```bash
./build/vivid build local-operators
```

Add **RingMod2** to a graph. Connect `osc1/output` → `ringmod2/input` → `audio_out/input`. You should hear amplitude modulation — a metallic, tremolo-like timbre.

Adjust `carrier_freq` to change the modulation rate:
- Low (20–50 Hz): tremolo (below hearing pitch)
- Mid (200–800 Hz): metallic sidebands audible as tones
- High (1000+ Hz): bell-like, inharmonic

## Step 5: Make carrier_freq drivable from Control

Like all `vivid::Param<float>` fields, `carrier_freq` automatically accepts Control wire connections. Connect an **LfoFr** → `ringmod2/carrier_freq` to sweep the carrier frequency in real time.

## Step 6: Accessing sample rate and block size

The `VividAudioContext` provides everything the DSP loop needs:

| Field | Type | Description |
|-------|------|-------------|
| `input_buffers[i]` | `const float*` | Input buffer i, `block_size` samples |
| `output_buffers[i]` | `float*` | Output buffer i to write into |
| `block_size` | `int` | Number of samples in this call |
| `sample_rate` | `double` | Current sample rate (e.g. 48000) |
| `param_values[i]` | `float` | Current value of param i |

Always use `ctx->sample_rate` rather than a hardcoded value — the device rate can change.

## Step 7: Stateful operators and hot-reload

Hot-reload destroys and recreates the operator instance. Any state in member variables (`phase_` in this case) resets on reload. This is usually fine for development. For stateful operators (delay lines, reverb buffers), initialize cleanly in the constructor and accept the reset.

## What's happening

The audio operator contract:
1. `collect_params` — declares parameters with ranges and defaults
2. `collect_ports` — declares typed audio I/O ports
3. `process_audio` — called at audio rate; writes `block_size` samples into each output buffer

The operator becomes a first-class citizen in the graph: it can be connected to other audio operators, have its params modulated by Control wires, and appear in the operator browser alongside built-in operators.

## Next steps

- [Writing a Custom GPU Operator](../custom-gpu-operator/) — same pattern for WGSL shaders
- Add analysis outputs: call `vivid::append_analysis_ports(out)` in `collect_ports` and `vivid::fill_analysis_ports(ctx, rms, peak)` in `process_audio` to expose rms/peak scalars for AV routing
