#include <vivid/audio/drum_kit.h>
#include <vivid/operator_registry.h>
#include <vivid/audio_graph.h>
#include <vivid/context.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

REGISTER_OPERATOR(DrumKit, "Audio Drums", "MIDI-controlled drum kit with multiple synthesizers", false);

DrumKit::DrumKit() {
    registerParam(volume);
    registerParam(kickVol);
    registerParam(snareVol);
    registerParam(hihatVol);
    registerParam(clapVol);
    registerParam(tomVol);
    registerParam(cymbalVol);

    // Initialize note mappings
    m_noteMapped.fill(false);
    m_noteMap.fill(DrumType::Kick);  // Default (won't be used due to noteMapped check)

    initDrums();
    setupDefaultMapping();
}

DrumKit::~DrumKit() = default;

void DrumKit::initDrums() {
    // Create all drum synthesizers
    m_kick = std::make_unique<Kick>();
    m_snare = std::make_unique<Snare>();
    m_closedHiHat = std::make_unique<HiHat>();
    m_openHiHat = std::make_unique<HiHat>();
    m_clap = std::make_unique<Clap>();
    m_lowTom = std::make_unique<Tom>();
    m_midTom = std::make_unique<Tom>();
    m_highTom = std::make_unique<Tom>();
    m_crash = std::make_unique<Cymbal>();
    m_ride = std::make_unique<Cymbal>();

    // Configure hi-hats
    m_closedHiHat->decay = 0.05f;   // Short decay for closed
    m_openHiHat->decay = 0.4f;      // Longer decay for open

    // Configure toms at different pitches
    m_lowTom->pitch = 80.0f;
    m_midTom->pitch = 120.0f;
    m_highTom->pitch = 180.0f;

    // Configure cymbals
    m_crash->decay = 1.5f;
    m_crash->tone = 0.6f;
    m_crash->shimmer = 0.6f;
    m_ride->decay = 0.8f;
    m_ride->tone = 0.4f;
    m_ride->shimmer = 0.3f;
}

void DrumKit::setupDefaultMapping() {
    // Clear all mappings
    m_noteMapped.fill(false);

    // Set up General MIDI drum mapping
    setNoteMapping(GM_KICK, DrumType::Kick);
    setNoteMapping(35, DrumType::Kick);  // Acoustic Bass Drum

    setNoteMapping(GM_SNARE, DrumType::Snare);
    setNoteMapping(GM_SNARE_RIM, DrumType::Snare);  // Side stick also triggers snare

    setNoteMapping(GM_CLOSED_HIHAT, DrumType::ClosedHiHat);
    setNoteMapping(GM_PEDAL_HIHAT, DrumType::ClosedHiHat);  // Pedal hi-hat

    setNoteMapping(GM_OPEN_HIHAT, DrumType::OpenHiHat);

    setNoteMapping(GM_CLAP, DrumType::Clap);

    setNoteMapping(GM_LOW_TOM, DrumType::LowTom);
    setNoteMapping(43, DrumType::LowTom);  // High Floor Tom

    setNoteMapping(GM_MID_TOM, DrumType::MidTom);
    setNoteMapping(47, DrumType::MidTom);  // Low-Mid Tom

    setNoteMapping(GM_HIGH_TOM, DrumType::HighTom);
    setNoteMapping(50, DrumType::HighTom);  // High Tom

    setNoteMapping(GM_CRASH, DrumType::Crash);
    setNoteMapping(57, DrumType::Crash);  // Crash 2

    setNoteMapping(GM_RIDE, DrumType::Ride);
    setNoteMapping(53, DrumType::Ride);  // Ride Bell
    setNoteMapping(59, DrumType::Ride);  // Ride 2
}

void DrumKit::setNoteMapping(uint8_t note, DrumType drum) {
    if (note < 128) {
        m_noteMap[note] = drum;
        m_noteMapped[note] = true;
    }
}

void DrumKit::clearNoteMapping(uint8_t note) {
    if (note < 128) {
        m_noteMapped[note] = false;
    }
}

void DrumKit::resetNoteMappings() {
    setupDefaultMapping();
}

void DrumKit::init(Context& ctx) {
    if (!beginInit()) return;

    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput(AUDIO_BLOCK_SIZE, 2, m_sampleRate);

    // Initialize all drums with the context
    // Each drum will allocate its own output buffer
    m_kick->init(ctx);
    m_snare->init(ctx);
    m_closedHiHat->init(ctx);
    m_openHiHat->init(ctx);
    m_clap->init(ctx);
    m_lowTom->init(ctx);
    m_midTom->init(ctx);
    m_highTom->init(ctx);
    m_crash->init(ctx);
    m_ride->init(ctx);
}

void DrumKit::process(Context& /*ctx*/) {
    // Processing happens in generateBlock on the audio thread
}

void DrumKit::generateBlock(uint32_t frameCount) {
    // Resize output if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    // Clear output buffer
    std::memset(m_output.samples, 0, frameCount * 2 * sizeof(float));

    // Generate and mix all drums
    auto mixDrum = [&](AudioOperator* drum, float vol) {
        if (!drum) return;
        drum->generateBlock(frameCount);
        const AudioBuffer* buf = drum->outputBuffer();
        if (!buf || !buf->samples) return;

        float mixVol = vol * static_cast<float>(volume);
        for (uint32_t i = 0; i < frameCount * 2; ++i) {
            m_output.samples[i] += buf->samples[i] * mixVol;
        }
    };

    mixDrum(m_kick.get(), static_cast<float>(kickVol));
    mixDrum(m_snare.get(), static_cast<float>(snareVol));
    mixDrum(m_closedHiHat.get(), static_cast<float>(hihatVol));
    mixDrum(m_openHiHat.get(), static_cast<float>(hihatVol));
    mixDrum(m_clap.get(), static_cast<float>(clapVol));
    mixDrum(m_lowTom.get(), static_cast<float>(tomVol));
    mixDrum(m_midTom.get(), static_cast<float>(tomVol));
    mixDrum(m_highTom.get(), static_cast<float>(tomVol));
    mixDrum(m_crash.get(), static_cast<float>(cymbalVol));
    mixDrum(m_ride.get(), static_cast<float>(cymbalVol));
}

void DrumKit::cleanup() {
    // Cleanup all drums
    m_kick->cleanup();
    m_snare->cleanup();
    m_closedHiHat->cleanup();
    m_openHiHat->cleanup();
    m_clap->cleanup();
    m_lowTom->cleanup();
    m_midTom->cleanup();
    m_highTom->cleanup();
    m_crash->cleanup();
    m_ride->cleanup();

    resetInit();
    releaseOutput();
}

// -----------------------------------------------------------------------------
// MidiReceiver Interface
// -----------------------------------------------------------------------------

void DrumKit::midiNoteOn(uint8_t note, float velocity, uint8_t /*channel*/) {
    if (note >= 128 || !m_noteMapped[note]) return;

    DrumType drum = m_noteMap[note];
    triggerDrum(drum, velocity);
}

void DrumKit::midiNoteOff(uint8_t /*note*/, float /*velocity*/, uint8_t /*channel*/) {
    // Drums are one-shot, no note-off handling needed
    // (Except potentially for open hi-hat choke, but that's handled via closed hi-hat)
}

void DrumKit::midiAllNotesOff() {
    // Drums are one-shot, nothing to do
}

void DrumKit::midiPanic() {
    // Reset all drums
    m_kick->reset();
    m_snare->reset();
    m_closedHiHat->reset();
    m_openHiHat->reset();
    m_clap->reset();
    m_lowTom->reset();
    m_midTom->reset();
    m_highTom->reset();
    m_crash->reset();
    m_ride->reset();
}

// -----------------------------------------------------------------------------
// Playback Control
// -----------------------------------------------------------------------------

void DrumKit::triggerDrum(DrumType drum, float velocity) {
    // Store velocity for visualization
    m_lastVelocity[static_cast<size_t>(drum)] = velocity;

    // Get the drum and trigger it
    AudioOperator* drumOp = getDrumForType(drum);
    if (drumOp) {
        // For closed hi-hat, choke open hi-hat first
        if (drum == DrumType::ClosedHiHat) {
            m_openHiHat->choke();
        }

        drumOp->trigger();
    }
}

AudioOperator* DrumKit::getDrumForType(DrumType type) {
    switch (type) {
        case DrumType::Kick:        return m_kick.get();
        case DrumType::Snare:       return m_snare.get();
        case DrumType::ClosedHiHat: return m_closedHiHat.get();
        case DrumType::OpenHiHat:   return m_openHiHat.get();
        case DrumType::Clap:        return m_clap.get();
        case DrumType::LowTom:      return m_lowTom.get();
        case DrumType::MidTom:      return m_midTom.get();
        case DrumType::HighTom:     return m_highTom.get();
        case DrumType::Crash:       return m_crash.get();
        case DrumType::Ride:        return m_ride.get();
        default:                    return nullptr;
    }
}

float DrumKit::getVolumeForType(DrumType type) {
    switch (type) {
        case DrumType::Kick:        return static_cast<float>(kickVol);
        case DrumType::Snare:       return static_cast<float>(snareVol);
        case DrumType::ClosedHiHat:
        case DrumType::OpenHiHat:   return static_cast<float>(hihatVol);
        case DrumType::Clap:        return static_cast<float>(clapVol);
        case DrumType::LowTom:
        case DrumType::MidTom:
        case DrumType::HighTom:     return static_cast<float>(tomVol);
        case DrumType::Crash:
        case DrumType::Ride:        return static_cast<float>(cymbalVol);
        default:                    return 1.0f;
    }
}

bool DrumKit::isActive() const {
    return m_kick->isActive() ||
           m_snare->isActive() ||
           m_closedHiHat->isActive() ||
           m_openHiHat->isActive() ||
           m_clap->isActive() ||
           m_lowTom->isActive() ||
           m_midTom->isActive() ||
           m_highTom->isActive() ||
           m_crash->isActive() ||
           m_ride->isActive();
}

// -----------------------------------------------------------------------------
// Inspection
// -----------------------------------------------------------------------------

InspectData DrumKit::inspect() const {
    auto data = AudioOperator::inspect();

    // Helper to compute RMS and peak from a drum's output buffer
    auto drumMetrics = [](const AudioOperator* drum, const char* prefix, InspectData& out) {
        if (!drum) return;
        const AudioBuffer* buf = drum->outputBuffer();
        if (!buf || !buf->isValid()) {
            out.set(std::string(prefix) + "_rms", 0.0f);
            out.set(std::string(prefix) + "_peak", 0.0f);
            return;
        }
        float sumSq = 0.0f;
        float peak = 0.0f;
        uint32_t count = buf->sampleCount();
        for (uint32_t i = 0; i < count; ++i) {
            float s = buf->samples[i];
            sumSq += s * s;
            float absS = std::abs(s);
            if (absS > peak) peak = absS;
        }
        out.set(std::string(prefix) + "_rms", std::sqrt(sumSq / static_cast<float>(count)));
        out.set(std::string(prefix) + "_peak", peak);
    };

    drumMetrics(m_kick.get(), "kick", data);
    drumMetrics(m_snare.get(), "snare", data);
    drumMetrics(m_closedHiHat.get(), "closed_hihat", data);
    drumMetrics(m_openHiHat.get(), "open_hihat", data);
    drumMetrics(m_clap.get(), "clap", data);
    drumMetrics(m_lowTom.get(), "low_tom", data);
    drumMetrics(m_midTom.get(), "mid_tom", data);
    drumMetrics(m_highTom.get(), "high_tom", data);
    drumMetrics(m_crash.get(), "crash", data);
    drumMetrics(m_ride.get(), "ride", data);

    // Per-drum volume settings
    data.set("kick_vol", static_cast<float>(kickVol));
    data.set("snare_vol", static_cast<float>(snareVol));
    data.set("hihat_vol", static_cast<float>(hihatVol));
    data.set("clap_vol", static_cast<float>(clapVol));
    data.set("tom_vol", static_cast<float>(tomVol));
    data.set("cymbal_vol", static_cast<float>(cymbalVol));

    return data;
}

// -----------------------------------------------------------------------------
// Visualization
// -----------------------------------------------------------------------------

bool DrumKit::drawVisualization(VizDrawList* dl, float minX, float minY,
                                 float maxX, float maxY) {
    float w = maxX - minX;
    float h = maxY - minY;

    // Background
    dl->AddRectFilled(VizVec2(minX, minY), VizVec2(maxX, maxY),
                      VIZ_COL32(40, 35, 50, 255));

    // Draw drum pads in a grid
    // Layout: 2 rows x 5 columns
    const int cols = 5;
    const int rows = 2;
    float padW = w / cols;
    float padH = h / rows;
    float padding = 2.0f;

    struct DrumPad {
        DrumType type;
        const char* label;
        uint32_t color;
    };

    DrumPad pads[] = {
        {DrumType::Kick, "K", VIZ_COL32(200, 80, 80, 255)},
        {DrumType::Snare, "S", VIZ_COL32(200, 200, 80, 255)},
        {DrumType::ClosedHiHat, "CH", VIZ_COL32(80, 200, 200, 255)},
        {DrumType::OpenHiHat, "OH", VIZ_COL32(80, 200, 150, 255)},
        {DrumType::Clap, "CP", VIZ_COL32(200, 80, 200, 255)},
        {DrumType::LowTom, "LT", VIZ_COL32(150, 100, 80, 255)},
        {DrumType::MidTom, "MT", VIZ_COL32(180, 120, 90, 255)},
        {DrumType::HighTom, "HT", VIZ_COL32(210, 140, 100, 255)},
        {DrumType::Crash, "CR", VIZ_COL32(255, 200, 100, 255)},
        {DrumType::Ride, "RD", VIZ_COL32(200, 180, 100, 255)},
    };

    for (int i = 0; i < 10; ++i) {
        int col = i % cols;
        int row = i / cols;
        float x = minX + col * padW + padding;
        float y = minY + row * padH + padding;
        float pw = padW - padding * 2;
        float ph = padH - padding * 2;

        // Check if drum is active
        AudioOperator* drum = getDrumForType(pads[i].type);
        bool active = drum && dynamic_cast<AudioOperator*>(drum) &&
                      (pads[i].type == DrumType::Kick ? m_kick->isActive() :
                       pads[i].type == DrumType::Snare ? m_snare->isActive() :
                       pads[i].type == DrumType::ClosedHiHat ? m_closedHiHat->isActive() :
                       pads[i].type == DrumType::OpenHiHat ? m_openHiHat->isActive() :
                       pads[i].type == DrumType::Clap ? m_clap->isActive() :
                       pads[i].type == DrumType::LowTom ? m_lowTom->isActive() :
                       pads[i].type == DrumType::MidTom ? m_midTom->isActive() :
                       pads[i].type == DrumType::HighTom ? m_highTom->isActive() :
                       pads[i].type == DrumType::Crash ? m_crash->isActive() :
                       m_ride->isActive());

        // Pad background
        uint32_t bgColor = active ? pads[i].color : VIZ_COL32(60, 55, 70, 255);
        dl->AddRectFilled(VizVec2(x, y), VizVec2(x + pw, y + ph), bgColor, 4.0f);

        // Border
        dl->AddRect(VizVec2(x, y), VizVec2(x + pw, y + ph),
                    VIZ_COL32(100, 100, 100, 255), 4.0f, 0, 1.0f);

        // Label
        VizTextSize ts = dl->CalcTextSize(pads[i].label);
        float tx = x + (pw - ts.x) * 0.5f;
        float ty = y + (ph - ts.y) * 0.5f;
        uint32_t textColor = active ? VIZ_COL32(0, 0, 0, 255) : VIZ_COL32(180, 180, 180, 255);
        dl->AddText(VizVec2(tx, ty), textColor, pads[i].label);
    }

    return true;
}

} // namespace vivid::audio
