# MultiSampler Roadmap

Full-featured multi-sample instrument supporting key zones, velocity layers, and round-robin. Based on Decent Sampler format analysis of real sample libraries.

---

## Reference Sample Packs

Analyzed from `assets/sample_packs/`:

| Library | Samples | Key Zones | Velocity Layers | Articulations |
|---------|---------|-----------|-----------------|---------------|
| 1781 Ganer Square Piano | 180 | ~11 per art. | 4 (pp/p/mf/f) | 8 (damper/buff combos) |
| Orgel Unterhaus | 115 | ~11 each | 1 | 10 organ stops |
| Lapsteel Articulations | 18 | 6 each | 1 | 3 (open/slide-down/slide-up) |

**Velocity layer ranges (Ganer Piano):**
- pp: 0-39
- p: 40-79
- mf: 80-110
- f: 111-127

---

## Current State: Sampler (Simpler-style)

`addons/vivid-audio/include/vivid/audio/sampler.h`

- Single sample loaded
- Pitch-shifts based on `rootNote`
- 8-voice polyphony with ADSR
- No key zones, no velocity layers

**Limitation:** Can't represent real instruments that have different samples per key/velocity.

---

## MultiSampler Design

### Core Classes

```cpp
/**
 * @brief Single sample region with key/velocity mapping
 */
struct SampleRegion {
    std::string path;
    int rootNote;                    // Original pitch of sample
    int loNote, hiNote;              // Key range
    int loVel, hiVel;                // Velocity range
    float volumeDb = 0.0f;
    float pan = 0.0f;                // -1 to 1
    int tuneCents = 0;

    // Loop settings
    bool loopEnabled = false;
    uint64_t loopStart = 0;
    uint64_t loopEnd = 0;
    uint64_t loopCrossfade = 0;

    // Runtime (populated after loading)
    std::vector<float> samples;      // Interleaved stereo
    uint32_t sampleFrames = 0;
    uint32_t sampleRate = 48000;
};

/**
 * @brief Group of samples sharing settings
 */
struct SampleGroup {
    std::string name;
    std::vector<SampleRegion> regions;

    // Shared envelope
    float attack = 0.0f;
    float decay = 0.0f;
    float sustain = 1.0f;
    float release = 0.3f;
    float volumeDb = 0.0f;

    // Key switch (optional)
    int keyswitch = -1;              // MIDI note to activate this group
};

/**
 * @brief Complete multi-sample instrument
 */
class MultiSampler : public AudioOperator {
public:
    // Parameters
    Param<float> volume{"volume", 0.8f, 0.0f, 2.0f};
    Param<int> maxVoices{"maxVoices", 16, 1, 64};

    // Envelope (can be overridden by group)
    Param<float> attack{"attack", 0.01f, 0.0f, 5.0f};
    Param<float> decay{"decay", 0.1f, 0.0f, 5.0f};
    Param<float> sustain{"sustain", 1.0f, 0.0f, 1.0f};
    Param<float> release{"release", 0.3f, 0.0f, 10.0f};

    // Velocity response curve
    Param<float> velCurve{"velCurve", 0.0f, -1.0f, 1.0f};  // -1=soft, 0=linear, 1=hard

    // Loading
    bool loadPreset(const std::string& jsonPath);
    bool loadDspreset(const std::string& dspresetPath);
    void addRegion(const SampleRegion& region);
    void addGroup(const SampleGroup& group);

    // Playback
    int noteOn(int midiNote, float velocity = 1.0f);
    void noteOff(int midiNote);
    void allNotesOff();
    void setKeyswitch(int note);     // Select active group

    // State
    int activeGroupIndex() const { return m_activeGroup; }
    int regionCount() const;

private:
    // Sample selection
    SampleRegion* findRegion(int note, int velocity);
    std::vector<SampleRegion*> findRegionsRoundRobin(int note, int velocity);

    std::vector<SampleGroup> m_groups;
    int m_activeGroup = 0;

    // Round-robin state per note
    std::unordered_map<int, int> m_roundRobinIndex;

    // Voice pool (similar to Sampler)
    struct Voice {
        int midiNote = -1;
        SampleRegion* region = nullptr;
        double position = 0.0;
        float pitch = 1.0f;
        float velocity = 1.0f;
        float pan = 0.0f;

        EnvelopeStage envStage = EnvelopeStage::Idle;
        float envValue = 0.0f;
        // ... rest of envelope state
    };

    std::vector<Voice> m_voices;
};
```

---

## JSON Preset Format

Output from `tools/dspreset_parser.py`:

```json
{
  "name": "1781 Ganer Square",
  "source_format": "DecentSampler",
  "samples": [
    {
      "path": "Samples/1 - No Damp No Buff/Loud/001_F0_loud.wav",
      "root_note": 41,
      "lo_note": 41,
      "hi_note": 43,
      "lo_vel": 80,
      "hi_vel": 110,
      "volume_db": 0.0,
      "pan": 0.0,
      "tune_cents": 0,
      "loop_enabled": false
    }
  ],
  "envelope": {
    "attack": 0.0,
    "decay": 0.0,
    "sustain": 1.0,
    "release": 0.3
  },
  "effects": {
    "filter_type": null,
    "filter_freq": 22000.0,
    "reverb_wet": 0.0
  }
}
```

---

## Sample Selection Algorithm

```cpp
SampleRegion* MultiSampler::findRegion(int note, int velocity) {
    int vel = static_cast<int>(velocity * 127);

    auto& group = m_groups[m_activeGroup];

    for (auto& region : group.regions) {
        if (note >= region.loNote && note <= region.hiNote &&
            vel >= region.loVel && vel <= region.hiVel) {
            return &region;
        }
    }

    // Fallback: find closest by note, ignore velocity
    SampleRegion* closest = nullptr;
    int closestDist = INT_MAX;

    for (auto& region : group.regions) {
        if (note >= region.loNote && note <= region.hiNote) {
            int dist = std::abs(note - region.rootNote);
            if (dist < closestDist) {
                closestDist = dist;
                closest = &region;
            }
        }
    }

    return closest;
}
```

---

## Pitch Calculation

Same as current Sampler, using sample's `rootNote`:

```cpp
float pitchFromNote(int playedNote, int rootNote) {
    int semitones = playedNote - rootNote;
    return std::pow(2.0f, semitones / 12.0f);
}
```

---

## Round-Robin Support

For instruments with multiple samples at same note/velocity:

```cpp
std::vector<SampleRegion*> findRegionsRoundRobin(int note, int velocity) {
    std::vector<SampleRegion*> matches;
    int vel = static_cast<int>(velocity * 127);

    for (auto& region : m_groups[m_activeGroup].regions) {
        if (note >= region.loNote && note <= region.hiNote &&
            vel >= region.loVel && vel <= region.hiVel) {
            matches.push_back(&region);
        }
    }

    if (matches.empty()) return {};

    // Round-robin
    int& rrIndex = m_roundRobinIndex[note];
    rrIndex = (rrIndex + 1) % matches.size();

    return { matches[rrIndex] };
}
```

---

## Keyswitch Support

For articulation switching (like Ganer Piano's 8 damper/buff combos):

```cpp
void MultiSampler::setKeyswitch(int note) {
    for (size_t i = 0; i < m_groups.size(); i++) {
        if (m_groups[i].keyswitch == note) {
            m_activeGroup = i;
            return;
        }
    }
}

// In noteOn:
int MultiSampler::noteOn(int note, float velocity) {
    // Check if this is a keyswitch
    for (size_t i = 0; i < m_groups.size(); i++) {
        if (m_groups[i].keyswitch == note) {
            m_activeGroup = i;
            return -1;  // No voice allocated for keyswitches
        }
    }

    // Normal note playback
    SampleRegion* region = findRegion(note, velocity);
    if (!region) return -1;

    // ... allocate voice, start playback
}
```

---

## Loading from JSON

```cpp
bool MultiSampler::loadPreset(const std::string& jsonPath) {
    std::ifstream file(jsonPath);
    if (!file) return false;

    json preset;
    file >> preset;

    SampleGroup group;
    group.name = preset["name"];

    // Load envelope
    if (preset.contains("envelope")) {
        auto& env = preset["envelope"];
        group.attack = env.value("attack", 0.0f);
        group.decay = env.value("decay", 0.0f);
        group.sustain = env.value("sustain", 1.0f);
        group.release = env.value("release", 0.3f);
    }

    // Load samples
    std::filesystem::path basePath = std::filesystem::path(jsonPath).parent_path();

    for (auto& s : preset["samples"]) {
        SampleRegion region;
        region.path = s["path"];
        region.rootNote = s["root_note"];
        region.loNote = s["lo_note"];
        region.hiNote = s["hi_note"];
        region.loVel = s.value("lo_vel", 0);
        region.hiVel = s.value("hi_vel", 127);
        region.volumeDb = s.value("volume_db", 0.0f);
        region.pan = s.value("pan", 0.0f);
        region.tuneCents = s.value("tune_cents", 0);
        region.loopEnabled = s.value("loop_enabled", false);

        // Resolve path relative to preset
        auto fullPath = basePath / region.path;
        loadWAV(fullPath, region);

        group.regions.push_back(std::move(region));
    }

    m_groups.push_back(std::move(group));
    return true;
}
```

---

## Files to Create

```
addons/vivid-audio/include/vivid/audio/
  multi_sampler.h          # MultiSampler class

addons/vivid-audio/src/
  multi_sampler.cpp        # Implementation

tools/
  dspreset_parser.py       # Already exists - converts .dspreset to JSON
```

---

## Example Usage

### Basic Loading

```cpp
void setup(Context& ctx) {
    auto& piano = chain.add<MultiSampler>("piano");
    piano.loadPreset("assets/sample_packs/1781 - Ganer Square Piano/vivid_presets.json");
    piano.attack = 0.01f;
    piano.release = 1.5f;
}

void update(Context& ctx) {
    auto& piano = chain.get<MultiSampler>("piano");

    // MIDI input
    for (auto& e : midi.events()) {
        if (e.type == MidiEventType::NoteOn) {
            piano.noteOn(e.note, e.velocity / 127.0f);
        } else if (e.type == MidiEventType::NoteOff) {
            piano.noteOff(e.note);
        }
    }

    ctx.chain().process();
}
```

### With Keyswitches

```cpp
// Ganer Piano keyswitches (from readme):
// C0 = No dampers, no buff
// D0 = No dampers with buff
// E0 = Treble damp, no buff
// etc.

void update(Context& ctx) {
    auto& piano = chain.get<MultiSampler>("piano");

    // Select articulation via keyswitch
    if (buttonPressed("buff")) {
        piano.setKeyswitch(26);  // D0
    } else {
        piano.setKeyswitch(24);  // C0
    }

    // Play normally
    piano.noteOn(60, 0.8f);  // Uses current articulation
}
```

### Manual Region Building

```cpp
void setup(Context& ctx) {
    auto& sampler = chain.add<MultiSampler>("custom");

    // Add regions manually
    SampleRegion kick;
    kick.path = "samples/kick.wav";
    kick.rootNote = 36;
    kick.loNote = 36;
    kick.hiNote = 36;
    sampler.addRegion(kick);

    SampleRegion snare;
    snare.path = "samples/snare.wav";
    snare.rootNote = 38;
    snare.loNote = 38;
    snare.hiNote = 38;
    sampler.addRegion(snare);
}
```

---

## Implementation Order

### Phase 1: Core MultiSampler
1. `SampleRegion` struct with key/vel ranges
2. `MultiSampler` class extending current Sampler voice logic
3. `findRegion()` sample selection
4. `loadPreset()` from JSON

### Phase 2: Advanced Features
5. Round-robin support
6. Keyswitch groups
7. Velocity curve adjustment
8. Direct .dspreset loading (wrap Python parser or reimplement in C++)

### Phase 3: Polish
9. Lazy sample loading (load on first noteOn)
10. Sample streaming for large libraries
11. GUI for region visualization

---

## Integration with Existing Code

**Reuse from Sampler:**
- Voice struct and management
- Envelope processing
- WAV loading (loadWAV)
- Interpolated sample playback (sampleAt)
- Voice stealing logic

**New code:**
- Sample region selection
- JSON preset loading
- Keyswitch handling
- Round-robin state
