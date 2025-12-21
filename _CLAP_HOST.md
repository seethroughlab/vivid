# CLAP Host Integration Plan

## Overview

Add CLAP (CLever Audio Plugin) hosting to vivid-audio, enabling use of third-party audio effects and instruments within vivid chains.

### Why CLAP over VST3?

| Aspect | CLAP | VST3 |
|--------|------|------|
| License | MIT (no fees, no membership) | Dual GPLv3/Proprietary |
| API | Pure C ABI (simple, stable) | C++ with COM-like interfaces |
| Threading | Explicit thread-safety contracts | Implicit, error-prone |
| Parameter modulation | Native per-voice modulation | Requires workarounds |
| Plugin scanning | Metadata without loading | Must instantiate to query |
| Adoption | Bitwig, Reaper, u-he, Surge | Universal but complex |

### Goals
- Load and run CLAP plugins (effects and instruments)
- Expose plugin parameters to vivid's parameter system
- Route audio through plugins in the chain
- Support MIDI/note input to instrument plugins
- Headless operation (no plugin GUI required, but optional)
- Leverage CLAP's per-voice modulation for expressive control

### Non-Goals (Initial Version)
- VST3/VST2/AU support (CLAP-only for simplicity)
- Plugin bridging (32-bit or sandboxed)
- CLAP extensions beyond core functionality

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      vivid Chain                            │
│                                                             │
│  ┌─────────┐    ┌──────────────┐    ┌─────────────┐        │
│  │ AudioIn │───▶│ CLAPEffect   │───▶│ Compressor  │───▶    │
│  └─────────┘    │ (Reverb.clap)│    └─────────────┘        │
│                 └──────────────┘                            │
│                                                             │
│  ┌─────────┐    ┌───────────────┐                          │
│  │Sequencer│───▶│CLAPInstrument │───▶──────────────────────▶│
│  └─────────┘    │ (Surge.clap)  │                          │
│                 └───────────────┘                           │
└─────────────────────────────────────────────────────────────┘
```

### Core Components

```
vivid-audio/
├── include/vivid/audio/
│   ├── clap_host/
│   │   ├── clap_host.h           # Plugin host singleton
│   │   ├── clap_plugin.h         # Individual plugin instance
│   │   ├── clap_effect.h         # AudioOperator wrapper for effects
│   │   ├── clap_instrument.h     # AudioOperator wrapper for instruments
│   │   └── clap_scanner.h        # Plugin discovery
│   └── ...
├── src/clap_host/
│   ├── clap_host.cpp
│   ├── clap_plugin.cpp
│   ├── clap_effect.cpp
│   ├── clap_instrument.cpp
│   └── clap_scanner.cpp
└── ...
```

## CLAP Core Concepts

### Plugin Lifecycle

```
┌─────────┐    ┌──────────┐    ┌─────────────────┐    ┌─────────┐
│  Load   │───▶│   Init   │───▶│    Activate     │───▶│ Process │
│ (.clap) │    │ (once)   │    │ (sample rate)   │    │ (loop)  │
└─────────┘    └──────────┘    └─────────────────┘    └─────────┘
                                        │                   │
                                        ▼                   ▼
                               ┌─────────────────┐  ┌──────────────┐
                               │   Deactivate    │◀─│Stop Processing│
                               └─────────────────┘  └──────────────┘
                                        │
                                        ▼
                               ┌─────────────────┐
                               │    Destroy      │
                               └─────────────────┘
```

### Key CLAP Structures

```cpp
// Plugin descriptor (metadata)
typedef struct clap_plugin_descriptor {
    clap_version_t clap_version;
    const char *id;           // "com.u-he.diva"
    const char *name;         // "Diva"
    const char *vendor;       // "u-he"
    const char *version;      // "1.4.4"
    const char *description;
    const char *const *features;  // CLAP_PLUGIN_FEATURE_*
} clap_plugin_descriptor_t;

// Audio buffer
typedef struct clap_audio_buffer {
    float **data32;           // 32-bit float channels
    double **data64;          // 64-bit double channels (alternative)
    uint32_t channel_count;
    uint32_t latency;
    uint64_t constant_mask;   // Optimization hint
} clap_audio_buffer_t;

// Process context
typedef struct clap_process {
    int64_t steady_time;
    uint32_t frames_count;
    const clap_event_transport_t *transport;
    const clap_audio_buffer_t *audio_inputs;
    clap_audio_buffer_t *audio_outputs;
    uint32_t audio_inputs_count;
    uint32_t audio_outputs_count;
    const clap_input_events_t *in_events;
    const clap_output_events_t *out_events;
} clap_process_t;
```

## API Design

### Loading a CLAP Effect

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio source
    auto& audioIn = chain.add<AudioIn>("audioIn");
    audioIn.volume = 1.0f;

    // Load plugin by path
    auto& reverb = chain.add<CLAPEffect>("reverb");
    reverb.load("/Library/Audio/Plug-Ins/CLAP/ValhallaRoom.clap");
    reverb.input("audioIn");
    reverb.loadPreset("Large Hall");

    // Or by scanned name
    auto& delay = chain.add<CLAPEffect>("delay");
    delay.loadByName("EchoBoy");  // Finds from scanned plugins
    delay.input("reverb");
    delay.setPluginParam("Time", 0.5f);
    delay.setPluginParam("Feedback", 0.3f);

    chain.audioOutput("delay");
}
```

### Loading a CLAP Instrument

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Sequencer for triggering notes
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;

    auto& seq = chain.add<Sequencer>("seq");
    seq.setPattern(0x1111);

    // CLAP instrument
    auto& synth = chain.add<CLAPInstrument>("synth");
    synth.load("/Library/Audio/Plug-Ins/CLAP/Surge XT.clap");
    synth.loadPreset("Init");

    // Effects chain
    auto& reverb = chain.add<CLAPEffect>("reverb");
    reverb.loadByName("ValhallaRoom");
    reverb.input("synth");

    chain.audioOutput("reverb");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& clock = chain.get<Clock>("clock");
    auto& seq = chain.get<Sequencer>("seq");
    auto& synth = chain.get<CLAPInstrument>("synth");

    if (clock.triggered()) {
        seq.advance();
        if (seq.triggered()) {
            synth.noteOn(0, 60, 0.8f);  // channel, note, velocity
        }
    }
}
```

### Parameter Modulation (CLAP Advantage)

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& synth = chain.add<CLAPInstrument>("synth");
    synth.loadByName("Diva");

    auto& lfo = chain.add<LFO>("lfo");
    lfo.rate = 2.0f;
    lfo.shape = LFOShape::Sine;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& synth = chain.get<CLAPInstrument>("synth");
    auto& lfo = chain.get<LFO>("lfo");

    // Per-voice modulation - non-destructive offset on top of automation
    // CLAP supports this natively, unlike VST3
    synth.modulateParam("Filter Cutoff", lfo.value() * 0.3f);
}
```

### MIDI Integration (vivid-midi)

CLAP instruments can be driven by hardware MIDI controllers or MIDI file playback via the `vivid-midi` addon.

```cpp
#include <vivid/midi/midi.h>

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Hardware MIDI input
    auto& midiIn = chain.add<MidiIn>("midi");
    midiIn.openPortByName("Arturia");  // Partial name match
    midiIn.channel = 0;  // 0 = omni (all channels)

    // CLAP instrument
    auto& surge = chain.add<CLAPInstrument>("surge");
    surge.loadByName("Surge XT");

    // Audio output
    auto& out = chain.add<AudioOutput>("out");
    out.input("surge");
    chain.audioOutput("out");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& midiIn = chain.get<MidiIn>("midi");
    auto& surge = chain.get<CLAPInstrument>("surge");

    // Route MIDI events to CLAP instrument
    for (const auto& e : midiIn.events()) {
        switch (e.type) {
            case MidiEventType::NoteOn:
                if (e.velocity > 0) {
                    surge.noteOn(e.channel, e.note, velocityToFloat(e.velocity));
                } else {
                    surge.noteOff(e.channel, e.note);
                }
                break;
            case MidiEventType::NoteOff:
                surge.noteOff(e.channel, e.note);
                break;
            case MidiEventType::PitchBend:
                // Route pitch bend to CLAP note expression
                // (per-voice tuning modulation)
                break;
            case MidiEventType::ControlChange:
                // Map CC to plugin parameter
                if (e.cc == CC::ModWheel) {
                    surge.modulateParam("Filter Cutoff", ccToFloat(e.value));
                }
                break;
            default:
                break;
        }
    }
}
```

MIDI file playback with tempo sync:

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;

    auto& player = chain.add<MidiFilePlayer>("player");
    player.load("song.mid");
    player.syncToClock(&clock);
    player.loop = true;
    player.play();

    auto& surge = chain.add<CLAPInstrument>("surge");
    surge.loadByName("Surge XT");

    chain.audioOutput("surge");
}

void update(Context& ctx) {
    auto& player = chain.get<MidiFilePlayer>("player");
    auto& surge = chain.get<CLAPInstrument>("surge");

    for (const auto& e : player.events()) {
        if (e.type == MidiEventType::NoteOn && e.velocity > 0) {
            surge.noteOn(e.channel, e.note, velocityToFloat(e.velocity));
        } else if (e.type == MidiEventType::NoteOff) {
            surge.noteOff(e.channel, e.note);
        }
    }
}
```

### Plugin Scanning

```cpp
// CLAP allows metadata queries without full instantiation
CLAPScanner scanner;
scanner.addSearchPath("/Library/Audio/Plug-Ins/CLAP");
scanner.scan();  // Fast - reads descriptors only

for (const auto& plugin : scanner.plugins()) {
    std::cout << plugin.name << " (" << plugin.vendor << ")\n";
    std::cout << "  ID: " << plugin.id << "\n";
    std::cout << "  Features: ";
    for (const auto& f : plugin.features) std::cout << f << " ";
    std::cout << "\n";
}
```

### State Management

CLAP plugins expose their state through `CLAP_EXT_STATE`, which allows saving and restoring the complete plugin configuration (parameters, internal state, loaded samples, etc.) as an opaque binary blob.

#### State vs Presets

| Concept | Extension | Description |
|---------|-----------|-------------|
| **State** | `CLAP_EXT_STATE` | Complete plugin snapshot (all parameters + internal data) |
| **Factory Presets** | `CLAP_EXT_PRESET_LOAD` | Built-in presets shipped with the plugin |
| **User Presets** | State saved to file | Your own saved configurations |

#### Saving and Loading Plugin State

```cpp
void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& reverb = chain.add<CLAPEffect>("reverb");
    reverb.loadByName("ValhallaRoom");

    // Load previously saved state (if exists)
    auto savedState = loadStateFromProject("reverb");
    if (!savedState.empty()) {
        reverb.loadState(savedState);
    }
}

void saveProject() {
    auto& reverb = chain.get<CLAPEffect>("reverb");

    // Get plugin state as binary blob
    std::vector<uint8_t> state = reverb.plugin()->saveState();

    // Save to your project file
    saveStateToProject("reverb", state);
}
```

#### Integration with Vivid Project Save/Load

When vivid saves a project, CLAP plugin states should be serialized alongside the chain configuration:

```cpp
// Project structure (conceptual)
struct ProjectData {
    std::string chainSource;                              // chain.cpp content
    std::map<std::string, std::vector<uint8_t>> pluginStates;  // operator name -> state
};

// On project save
void Chain::savePluginStates(ProjectData& project) {
    for (auto& [name, op] : m_operators) {
        if (auto* clap = dynamic_cast<CLAPEffect*>(op.get())) {
            if (clap->isLoaded()) {
                project.pluginStates[name] = clap->plugin()->saveState();
            }
        }
        if (auto* clap = dynamic_cast<CLAPInstrument*>(op.get())) {
            if (clap->isLoaded()) {
                project.pluginStates[name] = clap->plugin()->saveState();
            }
        }
    }
}

// On project load
void Chain::restorePluginStates(const ProjectData& project) {
    for (auto& [name, state] : project.pluginStates) {
        if (auto* clap = dynamic_cast<CLAPEffect*>(getOperator(name))) {
            clap->loadState(state);
        }
        if (auto* clap = dynamic_cast<CLAPInstrument*>(getOperator(name))) {
            clap->loadState(state);
        }
    }
}
```

#### Factory Presets

Some plugins provide factory presets that can be loaded by name or URI:

```cpp
auto& synth = chain.add<CLAPInstrument>("synth");
synth.loadByName("Surge XT");

// List available factory presets (if plugin supports preset-discovery)
for (const auto& preset : synth.plugin()->presetNames()) {
    std::cout << preset << "\n";
}

// Load a factory preset
synth.loadPreset("Init Saw");
```

#### State Context (Optional)

`CLAP_EXT_STATE_CONTEXT` allows plugins to save different states for different purposes:

- **For Preset**: Minimal state for sharing (no project-specific paths)
- **For Duplicate**: Full state for cloning within a project
- **For Project**: Full state including absolute file paths

```cpp
// Save state optimized for preset sharing
std::vector<uint8_t> presetState = plugin->saveStateForPreset();

// Save full state for project
std::vector<uint8_t> projectState = plugin->saveState();
```

## Dependencies

### CLAP Headers

```cmake
FetchContent_Declare(
    clap
    GIT_REPOSITORY https://github.com/free-audio/clap.git
    GIT_TAG        1.2.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(clap)

# Optional: C++ helpers for cleaner implementation
FetchContent_Declare(
    clap-helpers
    GIT_REPOSITORY https://github.com/free-audio/clap-helpers.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(clap-helpers)

target_include_directories(vivid-audio PRIVATE
    ${clap_SOURCE_DIR}/include
    ${clap-helpers_SOURCE_DIR}/include
)
```

### Required Headers
- `clap/clap.h` - Core API
- `clap/helpers/host.hh` - C++ host helper (optional)
- `clap/helpers/event-list.hh` - Event queue helper

## Implementation Phases

### Phase 1: Foundation

- [ ] 1.1 Add CLAP headers to CMakeLists.txt via FetchContent
- [ ] 1.2 Create `CLAPHost` singleton implementing `clap_host_t`
  - Provide host callbacks (get_extension, request_restart, etc.)
  - Implement thread-check extension for debugging
  - Implement log extension for plugin diagnostics
- [ ] 1.3 Create `CLAPPlugin` wrapper class
  - Load .clap bundle (dlopen/LoadLibrary)
  - Call `clap_entry.init()` and `clap_entry.get_factory()`
  - Instantiate plugin via `clap_plugin_factory.create_plugin()`
  - Query plugin descriptor (name, vendor, features)
- [ ] 1.4 Implement plugin lifecycle
  - `plugin->init(plugin)`
  - `plugin->activate(plugin, sample_rate, min_frames, max_frames)`
  - `plugin->start_processing(plugin)`
- [ ] 1.5 Test with Surge XT (open-source, well-tested)

### Phase 2: Audio Processing

- [ ] 2.1 Implement audio port discovery via `CLAP_EXT_AUDIO_PORTS`
  - Query input/output port configurations
  - Handle mono, stereo, and multichannel
- [ ] 2.2 Create `clap_process_t` for each audio block
  - Map vivid AudioBuffer to `clap_audio_buffer_t`
  - Handle 32-bit float (vivid's native format)
- [ ] 2.3 Implement process loop
  - Call `plugin->process(plugin, &process)`
  - Handle `CLAP_PROCESS_CONTINUE`, `CLAP_PROCESS_SLEEP`, `CLAP_PROCESS_TAIL`
- [ ] 2.4 Report latency via `CLAP_EXT_LATENCY`
- [ ] 2.5 Test audio passthrough and basic processing

### Phase 3: Parameter System

- [ ] 3.1 Query parameters via `CLAP_EXT_PARAMS`
  - `params->count(plugin)`
  - `params->get_info(plugin, index, &info)`
- [ ] 3.2 Map CLAP parameters to vivid `Param<T>` system
  - Handle parameter IDs (stable across sessions)
  - Support min/max/default values
- [ ] 3.3 Implement bidirectional parameter sync
  - Host→Plugin: `params->set_value()` in process context
  - Plugin→Host: Handle `CLAP_EVENT_PARAM_VALUE` output events
- [ ] 3.4 Implement parameter modulation via `CLAP_EXT_PARAMS` modulation
  - Non-destructive modulation offsets
  - Per-voice modulation for polyphonic instruments
- [ ] 3.5 Expose parameters to chain visualizer UI

### Phase 4: CLAPEffect Operator

- [ ] 4.1 Create `CLAPEffect` inheriting from `AudioOperator`
- [ ] 4.2 Implement `generateBlock()` to route audio through plugin
- [ ] 4.3 Handle mono/stereo/multichannel configurations
- [ ] 4.4 Implement bypass via parameter or direct buffer copy
- [ ] 4.5 Test with various effect plugins

### Phase 5: CLAPInstrument Operator

- [ ] 5.1 Create `CLAPInstrument` inheriting from `AudioOperator`
- [ ] 5.2 Implement note port discovery via `CLAP_EXT_NOTE_PORTS`
- [ ] 5.3 Convert vivid AudioEvents to CLAP note events
  - `CLAP_EVENT_NOTE_ON`, `CLAP_EVENT_NOTE_OFF`
  - `CLAP_EVENT_NOTE_EXPRESSION` for MPE-style control
- [ ] 5.4 Bridge vivid Sequencer output to CLAP events
- [ ] 5.5 Handle polyphonic instruments with voice management
- [ ] 5.6 Test with Surge XT, Vital, Dexed

### Phase 6: Plugin Scanner

- [ ] 6.1 Create `CLAPScanner` class
- [ ] 6.2 Implement platform-specific search paths
  - macOS: `/Library/Audio/Plug-Ins/CLAP`, `~/Library/Audio/Plug-Ins/CLAP`
  - Windows: `C:\Program Files\Common Files\CLAP`
  - Linux: `/usr/lib/clap`, `~/.clap`
- [ ] 6.3 Fast scanning via factory descriptor queries (no instantiation)
- [ ] 6.4 Cache scan results to JSON
- [ ] 6.5 Validate plugins match declared features

### Phase 7: Preset Management

- [ ] 7.1 Implement `CLAP_EXT_STATE` for save/load
  - `state->save(plugin, stream)`
  - `state->load(plugin, stream)`
- [ ] 7.2 Implement `CLAP_EXT_PRESET_LOAD` for factory presets
- [ ] 7.3 Query preset names via preset-discovery (if supported)
- [ ] 7.4 Integrate with vivid project save/load

### Phase 8: Plugin GUI (Optional)

- [ ] 8.1 Query GUI support via `CLAP_EXT_GUI`
- [ ] 8.2 Create floating window for plugin UI
  - `gui->create(plugin, CLAP_WINDOW_API_COCOA)` (macOS)
  - `gui->create(plugin, CLAP_WINDOW_API_WIN32)` (Windows)
  - `gui->create(plugin, CLAP_WINDOW_API_X11)` (Linux)
- [ ] 8.3 Handle resize via `gui->set_size()`, `gui->get_size()`
- [ ] 8.4 Implement `gui->show()` / `gui->hide()` toggle
- [ ] 8.5 Sync UI with parameter automation

### Phase 9: Advanced Features

- [ ] 9.1 Implement `CLAP_EXT_THREAD_POOL` for multicore plugins
- [ ] 9.2 Support `CLAP_EXT_VOICE_INFO` for voice count queries
- [ ] 9.3 Implement `CLAP_EXT_TAIL` for proper effect tail handling
- [ ] 9.4 Add `CLAP_EXT_RENDER` for offline rendering support

## Class Interfaces

### CLAPHost

```cpp
class CLAPHost {
public:
    static CLAPHost& instance();

    // Plugin loading
    std::unique_ptr<CLAPPlugin> loadPlugin(const std::filesystem::path& path,
                                            const std::string& pluginId = "");

    // Scanner access
    CLAPScanner& scanner();

    // Audio settings (applied to all plugins)
    void setSampleRate(double rate);
    void setBlockSize(uint32_t size);

    // Host interface for plugins
    const clap_host_t* clapHost() const;

private:
    CLAPHost();
    ~CLAPHost();

    // clap_host_t callbacks
    static const void* clapGetExtension(const clap_host_t*, const char* id);
    static void clapRequestRestart(const clap_host_t*);
    static void clapRequestProcess(const clap_host_t*);
    static void clapRequestCallback(const clap_host_t*);

    clap_host_t m_host;
    double m_sampleRate = 48000.0;
    uint32_t m_blockSize = 512;
};
```

### CLAPPlugin

```cpp
class CLAPPlugin {
public:
    // Lifecycle
    bool init();
    bool activate(double sampleRate, uint32_t minFrames, uint32_t maxFrames);
    void deactivate();
    bool startProcessing();
    void stopProcessing();
    void destroy();

    // Info
    std::string id() const;
    std::string name() const;
    std::string vendor() const;
    std::vector<std::string> features() const;
    bool isInstrument() const;
    bool isEffect() const;
    bool hasGui() const;

    // Audio
    void process(const clap_process_t* process);
    clap_process_status processBlock(float** inputs, float** outputs,
                                      uint32_t numFrames,
                                      const std::vector<AudioEvent>& events);
    uint32_t latencySamples() const;
    uint32_t tailSamples() const;

    // Parameters
    uint32_t paramCount() const;
    CLAPParamInfo paramInfo(uint32_t index) const;
    CLAPParamInfo paramInfoById(clap_id id) const;
    double getParam(clap_id id) const;
    void setParam(clap_id id, double value);
    void modulateParam(clap_id id, double amount);  // Non-destructive

    // Notes (for instruments)
    void noteOn(int16_t port, int16_t channel, int16_t key, double velocity);
    void noteOff(int16_t port, int16_t channel, int16_t key, double velocity);
    void noteExpression(int16_t port, int16_t channel, int16_t key,
                        clap_note_expression expressionId, double value);

    // Presets
    std::vector<uint8_t> saveState() const;
    bool loadState(const std::vector<uint8_t>& data);
    void loadPreset(const std::string& uri);

    // GUI
    bool createGui(void* parentWindow);
    void destroyGui();
    void showGui();
    void hideGui();
    std::pair<uint32_t, uint32_t> guiSize() const;

private:
    const clap_plugin_t* m_plugin = nullptr;
    const clap_plugin_entry_t* m_entry = nullptr;
    void* m_library = nullptr;  // dlopen handle

    // Cached extensions
    const clap_plugin_params_t* m_params = nullptr;
    const clap_plugin_audio_ports_t* m_audioPorts = nullptr;
    const clap_plugin_note_ports_t* m_notePorts = nullptr;
    const clap_plugin_state_t* m_state = nullptr;
    const clap_plugin_gui_t* m_gui = nullptr;
};
```

### CLAPEffect (AudioOperator)

```cpp
class CLAPEffect : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<bool> bypass{"bypass", false};  ///< Bypass plugin processing

    /// @}
    // -------------------------------------------------------------------------

    CLAPEffect();
    ~CLAPEffect() override;

    // -------------------------------------------------------------------------
    /// @name Plugin Loading
    /// @{

    void load(const std::string& path);           // Load by file path
    void loadByName(const std::string& name);     // Load by scanned name
    void loadById(const std::string& id);         // Load by CLAP ID
    void loadPreset(const std::string& name);     // Load factory preset
    void loadState(const std::vector<uint8_t>& data);  // Load saved state

    /// @}
    // -------------------------------------------------------------------------
    /// @name Plugin Parameters
    /// @{

    void setPluginParam(const std::string& name, float value);
    float getPluginParam(const std::string& name) const;
    void modulateParam(const std::string& name, float amount);  // Non-destructive
    std::vector<CLAPParamInfo> pluginParams() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "CLAPEffect"; }

    void generateBlock(uint32_t frameCount) override;

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Plugin Access
    /// @{

    CLAPPlugin* plugin() { return m_plugin.get(); }
    const CLAPPlugin* plugin() const { return m_plugin.get(); }
    bool isLoaded() const { return m_plugin != nullptr; }

    /// @}

private:
    std::unique_ptr<CLAPPlugin> m_plugin;
    std::string m_pluginPath;

    // Process buffers
    std::vector<float*> m_inputPtrs;
    std::vector<float*> m_outputPtrs;
    clap_audio_buffer_t m_clapInputs;
    clap_audio_buffer_t m_clapOutputs;
};
```

### CLAPInstrument (AudioOperator)

```cpp
class CLAPInstrument : public AudioOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> volume{"volume", 1.0f, 0.0f, 2.0f};  ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    CLAPInstrument();
    ~CLAPInstrument() override;

    // -------------------------------------------------------------------------
    /// @name Plugin Loading
    /// @{

    void load(const std::string& path);
    void loadByName(const std::string& name);
    void loadById(const std::string& id);
    void loadPreset(const std::string& name);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Note Control
    /// @{

    void noteOn(int channel, int note, float velocity);
    void noteOff(int channel, int note, float velocity = 0.0f);
    void allNotesOff();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Plugin Parameters
    /// @{

    void setPluginParam(const std::string& name, float value);
    float getPluginParam(const std::string& name) const;
    void modulateParam(const std::string& name, float amount);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "CLAPInstrument"; }

    void generateBlock(uint32_t frameCount) override;
    void handleEvent(const AudioEvent& event) override;

    /// @}

private:
    std::unique_ptr<CLAPPlugin> m_plugin;
    std::vector<clap_event_note_t> m_pendingNotes;
};
```

## Platform Considerations

### macOS
- .clap bundles are directories with `Contents/MacOS/<name>` binary
- Entry point: `clap_entry` symbol
- GUI: `CLAP_WINDOW_API_COCOA` with NSView
- Code signing may affect loading

### Windows
- .clap files are DLLs with `.clap` extension
- Entry point: `clap_entry` exported symbol
- GUI: `CLAP_WINDOW_API_WIN32` with HWND

### Linux
- .clap files are shared objects
- Entry point: `clap_entry` symbol
- GUI: `CLAP_WINDOW_API_X11` with Window

### Bundle Loading

```cpp
// macOS/Linux
void* lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
auto entry = (const clap_plugin_entry_t*)dlsym(lib, "clap_entry");

// Windows
HMODULE lib = LoadLibraryW(path.c_str());
auto entry = (const clap_plugin_entry_t*)GetProcAddress(lib, "clap_entry");

// Initialize entry
entry->init(path.c_str());

// Get plugin factory
auto factory = (const clap_plugin_factory_t*)
    entry->get_factory(CLAP_PLUGIN_FACTORY_ID);

// Create plugin instance
const clap_plugin_t* plugin = factory->create_plugin(factory, &host, plugin_id);
```

## Event Handling

### Input Events (Host→Plugin)

```cpp
// Build event list for process call
clap_event_list events;

// Parameter change
clap_event_param_value_t param_event = {
    .header = {
        .size = sizeof(clap_event_param_value_t),
        .time = sample_offset,
        .space_id = CLAP_CORE_EVENT_SPACE_ID,
        .type = CLAP_EVENT_PARAM_VALUE,
        .flags = 0,
    },
    .param_id = param_id,
    .cookie = nullptr,
    .note_id = -1,
    .port_index = -1,
    .channel = -1,
    .key = -1,
    .value = new_value,
};
events.push(&param_event.header);

// Note on
clap_event_note_t note_event = {
    .header = {
        .size = sizeof(clap_event_note_t),
        .time = sample_offset,
        .space_id = CLAP_CORE_EVENT_SPACE_ID,
        .type = CLAP_EVENT_NOTE_ON,
        .flags = 0,
    },
    .note_id = note_id,
    .port_index = 0,
    .channel = 0,
    .key = midi_note,
    .velocity = velocity,  // 0.0 to 1.0
};
events.push(&note_event.header);
```

## Testing Strategy

### Unit Tests
- Plugin loading/unloading
- Parameter get/set/modulate
- Audio processing (null test, gain test)
- Note event handling
- State save/load

### Integration Tests
- Full chain with CLAP plugins
- Parameter automation from LFO
- Multiple plugins in chain
- Preset switching during playback

### Test Plugins (Open Source)
- [Surge XT](https://surge-synthesizer.github.io/) - Full synthesizer
- [Vital](https://vital.audio/) - Wavetable synth (free version)
- [Dexed](https://asb2m10.github.io/dexed/) - DX7 emulator
- [Chow DSP plugins](https://chowdsp.com/) - Various effects

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Plugin crashes | CLAP's clear threading model reduces crashes; consider process isolation later |
| Fewer plugins than VST3 | Growing ecosystem; major vendors adopting (u-he, Surge, Bitwig) |
| Extension compatibility | Start with core extensions; add others incrementally |
| Thread safety | CLAP explicitly documents thread requirements; use thread-check extension |
| Performance | Profile with real plugins; leverage constant_mask optimization |

## Resources

- [CLAP Specification](https://github.com/free-audio/clap) - Official repo
- [CLAP Helpers](https://github.com/free-audio/clap-helpers) - C++ implementation helpers
- [cleveraudio.org](https://cleveraudio.org/) - Official site
- [u-he CLAP Info](https://u-he.com/community/clap/) - Background and rationale
- [Surge XT Source](https://github.com/surge-synthesizer/surge) - Reference CLAP plugin
- [CLAP Validator](https://github.com/free-audio/clap-validator) - Test plugin compliance
