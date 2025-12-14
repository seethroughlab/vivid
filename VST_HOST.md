# VST3 Host Integration Plan

## Overview

Add VST3 plugin hosting to vivid-audio, enabling use of third-party audio effects and instruments within vivid chains.

### Goals
- Load and run VST3 plugins (effects and instruments)
- Expose plugin parameters to vivid's parameter system
- Route audio through plugins in the chain
- Support MIDI input to instrument plugins
- Headless operation (no plugin GUI required, but optional)

### Non-Goals (Initial Version)
- VST2 support (legacy, licensing issues)
- AU support (macOS only, adds complexity)
- AAX support (Pro Tools only)
- CLAP support (future consideration)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     vivid Chain                         │
│                                                         │
│  ┌─────────┐    ┌─────────────┐    ┌─────────────┐     │
│  │ AudioIn │───▶│ VSTEffect   │───▶│ Compressor  │───▶ │
│  └─────────┘    │ (Reverb.vst3)│    └─────────────┘     │
│                 └─────────────┘                         │
│                                                         │
│  ┌─────────┐    ┌─────────────┐                        │
│  │ MIDI    │───▶│ VSTInstrument│───▶───────────────────▶│
│  │ Sequencer│   │ (Synth.vst3) │                        │
│  └─────────┘    └─────────────┘                         │
└─────────────────────────────────────────────────────────┘
```

### Core Components

```
vivid-audio/
├── include/vivid/audio/
│   ├── vst/
│   │   ├── vst_host.h          # Plugin host singleton
│   │   ├── vst_plugin.h        # Individual plugin instance
│   │   ├── vst_effect.h        # AudioOperator wrapper for effects
│   │   ├── vst_instrument.h    # AudioOperator wrapper for instruments
│   │   └── vst_scanner.h       # Plugin discovery/validation
│   └── ...
├── src/vst/
│   ├── vst_host.cpp
│   ├── vst_plugin.cpp
│   ├── vst_effect.cpp
│   ├── vst_instrument.cpp
│   └── vst_scanner.cpp
└── ...
```

## API Design

### Loading a VST Effect

```cpp
void setup(Context& ctx) {
    // Load plugin by path
    chain.add<VSTEffect>("reverb")
        .load("/Library/Audio/Plug-Ins/VST3/ValhallaRoom.vst3")
        .preset("Large Hall");

    // Or by scanned name
    chain.add<VSTEffect>("delay")
        .plugin("EchoBoy")  // Finds from scanned plugins
        .setParam("Time", 0.5f)
        .setParam("Feedback", 0.3f);
}
```

### Loading a VST Instrument

```cpp
void setup(Context& ctx) {
    chain.add<Sequencer>("seq")
        .bpm(120)
        .pattern("kick", "x...x...x...x...");

    chain.add<VSTInstrument>("synth")
        .plugin("Serum")
        .preset("Init")
        .input("seq");  // Receives MIDI from sequencer
}
```

### Parameter Discovery

```cpp
void setup(Context& ctx) {
    auto& reverb = chain.add<VSTEffect>("reverb")
        .load("ValhallaRoom.vst3");

    // List available parameters
    for (const auto& param : reverb.pluginParams()) {
        std::cout << param.name << ": " << param.value
                  << " [" << param.min << "-" << param.max << "]\n";
    }
}
```

### Plugin Scanning

```cpp
// Typically done once at startup or on demand
VSTScanner scanner;
scanner.addSearchPath("/Library/Audio/Plug-Ins/VST3");
scanner.addSearchPath("~/Library/Audio/Plug-Ins/VST3");
scanner.scan();  // Async, validates each plugin

for (const auto& plugin : scanner.plugins()) {
    std::cout << plugin.name << " (" << plugin.vendor << ")\n";
    std::cout << "  Type: " << (plugin.isInstrument ? "Instrument" : "Effect") << "\n";
    std::cout << "  Path: " << plugin.path << "\n";
}
```

## Dependencies

### VST3 SDK

```cmake
FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG        v3.7.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(vst3sdk)

target_link_libraries(vivid-audio PRIVATE
    sdk
    sdk_hosting
)
```

### SDK Components Needed
- `pluginterfaces` - Core VST3 interfaces
- `base` - Utility classes
- `sdk_hosting` - Host-side helpers

## Implementation Phases

### Phase 1: Foundation

- [ ] 1.1 Add VST3 SDK to CMakeLists.txt via FetchContent
- [ ] 1.2 Create `VSTHost` singleton class
  - Initialize VST3 hosting infrastructure
  - Manage plugin module loading/unloading
  - Handle component factory registration
- [ ] 1.3 Create `VSTPlugin` base class
  - Load .vst3 bundle
  - Instantiate IComponent and IAudioProcessor
  - Query plugin info (name, vendor, category)
  - Setup audio buses (stereo in/out initially)
- [ ] 1.4 Basic audio processing
  - Activate/deactivate processor
  - Process audio buffers
  - Handle sample rate changes
- [ ] 1.5 Test with a known simple plugin

### Phase 2: Parameter System

- [ ] 2.1 Query plugin parameters via IEditController
- [ ] 2.2 Map VST3 parameters to vivid Param<T> system
- [ ] 2.3 Implement `setParam()` / `getParam()` bridging
- [ ] 2.4 Expose parameters to chain visualizer UI
- [ ] 2.5 Support parameter automation from LFO/envelope operators
- [ ] 2.6 Test parameter changes during playback

### Phase 3: VSTEffect Operator

- [ ] 3.1 Create `VSTEffect` class inheriting from `AudioOperator`
- [ ] 3.2 Implement `process()` to route audio through plugin
- [ ] 3.3 Handle mono/stereo/multichannel configurations
- [ ] 3.4 Implement latency compensation reporting
- [ ] 3.5 Add bypass functionality
- [ ] 3.6 Test with various effect plugins (reverb, delay, EQ)

### Phase 4: VSTInstrument Operator

- [ ] 4.1 Create `VSTInstrument` class inheriting from `AudioOperator`
- [ ] 4.2 Implement MIDI event input handling
- [ ] 4.3 Bridge vivid Sequencer output to VST3 event input
- [ ] 4.4 Handle note on/off, CC, pitch bend
- [ ] 4.5 Support polyphonic instruments
- [ ] 4.6 Test with various instrument plugins

### Phase 5: Plugin Scanner

- [ ] 5.1 Create `VSTScanner` class
- [ ] 5.2 Implement platform-specific search paths
  - macOS: `/Library/Audio/Plug-Ins/VST3`, `~/Library/Audio/Plug-Ins/VST3`
  - Windows: `C:\Program Files\Common Files\VST3`
  - Linux: `/usr/lib/vst3`, `~/.vst3`
- [ ] 5.3 Validate plugins (load, query info, unload)
- [ ] 5.4 Cache scan results to JSON
- [ ] 5.5 Background scanning with progress callback
- [ ] 5.6 Blacklist crashed/invalid plugins

### Phase 6: Preset Management

- [ ] 6.1 List plugin factory presets
- [ ] 6.2 Load preset by name or index
- [ ] 6.3 Save/load plugin state (vstpreset format)
- [ ] 6.4 Integrate with vivid project save/load

### Phase 7: Plugin GUI (Optional)

- [ ] 7.1 Query plugin for IPlugView
- [ ] 7.2 Create platform window for plugin UI
  - macOS: NSView embedding
  - Windows: HWND embedding
  - Linux: X11 embedding
- [ ] 7.3 Handle resize requests
- [ ] 7.4 Sync UI state with parameter changes
- [ ] 7.5 Toggle GUI visibility from chain visualizer

### Phase 8: Robustness

- [ ] 8.1 Handle plugin crashes gracefully (separate process?)
- [ ] 8.2 Timeout for unresponsive plugins
- [ ] 8.3 Thread safety for parameter changes
- [ ] 8.4 Memory leak testing
- [ ] 8.5 Performance profiling

## Class Interfaces

### VSTHost

```cpp
class VSTHost {
public:
    static VSTHost& instance();

    // Plugin loading
    std::unique_ptr<VSTPlugin> loadPlugin(const std::filesystem::path& path);

    // Scanner access
    VSTScanner& scanner();

    // Audio settings
    void setSampleRate(double rate);
    void setBlockSize(int size);

private:
    VSTHost();
    ~VSTHost();

    class Impl;
    std::unique_ptr<Impl> m_impl;
};
```

### VSTPlugin

```cpp
class VSTPlugin {
public:
    // Info
    std::string name() const;
    std::string vendor() const;
    bool isInstrument() const;
    bool hasEditor() const;

    // Audio
    void activate();
    void deactivate();
    void process(float** inputs, float** outputs, int numSamples);
    int latencySamples() const;

    // Parameters
    int paramCount() const;
    VSTParamInfo paramInfo(int index) const;
    float getParam(int index) const;
    void setParam(int index, float value);
    float getParamByName(const std::string& name) const;
    void setParamByName(const std::string& name, float value);

    // MIDI (for instruments)
    void sendNoteOn(int channel, int note, float velocity);
    void sendNoteOff(int channel, int note);
    void sendCC(int channel, int cc, float value);

    // Presets
    std::vector<std::string> presetNames() const;
    void loadPreset(const std::string& name);
    void loadPreset(int index);
    std::vector<uint8_t> saveState() const;
    void loadState(const std::vector<uint8_t>& data);

    // GUI
    void openEditor(void* parentWindow);
    void closeEditor();
    std::pair<int, int> editorSize() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
```

### VSTEffect (AudioOperator)

```cpp
class VSTEffect : public AudioOperator {
public:
    // Fluent API
    VSTEffect& load(const std::string& path);
    VSTEffect& plugin(const std::string& name);  // By scanned name
    VSTEffect& preset(const std::string& name);
    VSTEffect& setPluginParam(const std::string& name, float value);

    // AudioOperator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "VSTEffect"; }

    // Parameter bridging
    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    // Plugin access
    VSTPlugin* plugin();
    std::vector<VSTParamInfo> pluginParams() const;

private:
    std::unique_ptr<VSTPlugin> m_plugin;
    Param<std::string> m_pluginPath{"path", ""};
    Param<std::string> m_presetName{"preset", ""};
    Param<bool> m_bypass{"bypass", false};
};
```

## Platform Considerations

### macOS
- .vst3 bundles are directories
- Need to handle code signing/notarization for some plugins
- Plugins may use AudioUnit internally
- Some plugins require specific entitlements

### Windows
- .vst3 files are single-file bundles or directories
- COM-based loading
- May need to handle 32-bit plugins via bridging (out of scope)

### Linux
- .vst3 directories with .so inside
- X11 for GUI embedding
- Less plugin availability

## Testing Strategy

### Unit Tests
- Plugin loading/unloading
- Parameter get/set
- Audio processing (null test, gain test)
- MIDI event handling

### Integration Tests
- Full chain with VST plugins
- Parameter automation
- Preset loading
- Multiple plugins in chain

### Test Plugins
- Use free/open-source VST3 plugins for CI:
  - [Surge](https://surge-synthesizer.github.io/) - Full-featured synth
  - [Vital](https://vital.audio/) - Wavetable synth (free version)
  - [Dexed](https://asb2m10.github.io/dexed/) - DX7 emulator
  - [OrilRiver](https://www.kvraudio.com/product/orilriver-by-denis-tihanov) - Reverb

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Plugin crashes vivid | Consider out-of-process hosting (complex) or accept graceful degradation |
| Licensing issues | VST3 SDK is dual-licensed (proprietary + GPLv3), vivid would need to comply |
| Plugin compatibility | Extensive testing, blacklist problematic plugins |
| Performance overhead | Profile, optimize buffer handling, consider SIMD |
| Thread safety | Clear ownership model, lock-free where possible |

## Future Extensions

- **CLAP Support** - Add alongside VST3 with common abstraction
- **Plugin Chainer** - Multiple plugins in a single operator
- **Sidechain** - Route audio between operators for sidechain compression
- **Plugin Bridge** - Run 32-bit plugins or plugins in separate process
- **Remote Plugins** - Network-based plugin hosting

## Resources

- [VST3 SDK Documentation](https://steinbergmedia.github.io/vst3_doc/)
- [VST3 SDK GitHub](https://github.com/steinbergmedia/vst3sdk)
- [VST3 Hosting Guide](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical+Documentation/VST+3+Hosting/Index.html)
- [validator tool](https://github.com/steinbergmedia/vst3sdk/tree/master/bin) - Test plugin compliance
