// Vivid Audio - WavetableSynth Implementation

#include <vivid/audio/wavetable_synth.h>
#include <vivid/audio/notes.h>
#include <vivid/context.h>
#include <cstring>
#include <limits>
#include <algorithm>
#include <cmath>

namespace vivid::audio {

WavetableSynth::WavetableSynth() {
    registerParam(position);
    registerParam(maxVoices);
    registerParam(detune);
    registerParam(volume);
    registerParam(attack);
    registerParam(decay);
    registerParam(sustain);
    registerParam(release);

    // Unison parameters
    registerParam(unisonVoices);
    registerParam(unisonSpread);
    registerParam(unisonStereo);

    // Sub oscillator
    registerParam(subLevel);
    registerParam(subOctave);

    // Portamento
    registerParam(portamento);

    // Velocity sensitivity
    registerParam(velToVolume);
    registerParam(velToAttack);

    // Warp
    registerParam(warpAmount);

    // Filter
    registerParam(filterCutoff);
    registerParam(filterResonance);
    registerParam(filterKeytrack);

    // Filter envelope
    registerParam(filterAttack);
    registerParam(filterDecay);
    registerParam(filterSustain);
    registerParam(filterRelease);
    registerParam(filterEnvAmount);

    // Pre-allocate voices (8 notes * 8 unison = 64 max)
    m_voices.resize(64);

    // Load default wavetable
    loadBuiltin(BuiltinTable::Basic);
}

void WavetableSynth::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_initialized = true;
}

void WavetableSynth::process(Context& ctx) {
    if (!m_initialized) return;
    // Audio generation happens in generateBlock()
}

void WavetableSynth::cleanup() {
    m_voices.clear();
    m_wavetable.clear();
    releaseOutput();
    m_initialized = false;
}

// =============================================================================
// Wavetable Loading
// =============================================================================

void WavetableSynth::loadBuiltin(BuiltinTable table) {
    switch (table) {
        case BuiltinTable::Basic:   generateBasicTable();   break;
        case BuiltinTable::Analog:  generateAnalogTable();  break;
        case BuiltinTable::Digital: generateDigitalTable(); break;
        case BuiltinTable::Vocal:   generateVocalTable();   break;
        case BuiltinTable::Texture: generateTextureTable(); break;
        case BuiltinTable::PWM:     generatePWMTable();     break;
    }
}

bool WavetableSynth::loadWavetable(const std::string& path, uint32_t framesPerCycle) {
    // TODO: Implement file loading using stb_vorbis or similar
    // For now, just generate the basic table
    generateBasicTable();
    return false;
}

void WavetableSynth::generateFromHarmonics(const std::vector<float>& harmonics, uint32_t frameCount) {
    if (harmonics.empty() || frameCount == 0) return;

    m_frameCount = std::min(frameCount, MAX_FRAMES);
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    // Each frame adds more harmonics
    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* frameData = m_wavetable.data() + frame * SAMPLES_PER_FRAME;

        // How many harmonics to include in this frame
        float framePosition = static_cast<float>(frame) / static_cast<float>(m_frameCount - 1);
        uint32_t numHarmonics = std::max(1u,
            static_cast<uint32_t>(framePosition * harmonics.size()));

        // Generate additive synthesis waveform
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sample = 0.0f;

            for (uint32_t h = 0; h < numHarmonics && h < harmonics.size(); ++h) {
                float harmonic = static_cast<float>(h + 1);
                sample += harmonics[h] * std::sin(phase * TWO_PI * harmonic);
            }

            // Normalize
            frameData[i] = sample / static_cast<float>(numHarmonics);
        }
    }
}

void WavetableSynth::generateFromFormula(
    std::function<float(float phase, float position)> fn, uint32_t frameCount)
{
    if (!fn || frameCount == 0) return;

    m_frameCount = std::min(frameCount, MAX_FRAMES);
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* frameData = m_wavetable.data() + frame * SAMPLES_PER_FRAME;
        float pos = static_cast<float>(frame) / static_cast<float>(m_frameCount - 1);

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            frameData[i] = std::clamp(fn(phase, pos), -1.0f, 1.0f);
        }
    }
}

// =============================================================================
// Built-in Wavetable Generators
// =============================================================================

void WavetableSynth::generateBasicTable() {
    // Sine -> Triangle -> Saw -> Square morph
    m_frameCount = 8;
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* data = m_wavetable.data() + frame * SAMPLES_PER_FRAME;
        float t = static_cast<float>(frame) / static_cast<float>(m_frameCount - 1);

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);

            // Basic waveforms
            float sine = std::sin(phase * TWO_PI);
            float triangle = 4.0f * std::abs(phase - 0.5f) - 1.0f;
            float saw = 2.0f * phase - 1.0f;
            float square = phase < 0.5f ? 1.0f : -1.0f;

            // Morph: 0.0-0.33 sine->triangle, 0.33-0.66 triangle->saw, 0.66-1.0 saw->square
            float sample;
            if (t < 0.333f) {
                float blend = t / 0.333f;
                sample = sine * (1.0f - blend) + triangle * blend;
            } else if (t < 0.666f) {
                float blend = (t - 0.333f) / 0.333f;
                sample = triangle * (1.0f - blend) + saw * blend;
            } else {
                float blend = (t - 0.666f) / 0.334f;
                sample = saw * (1.0f - blend) + square * blend;
            }

            data[i] = sample;
        }
    }
}

void WavetableSynth::generateAnalogTable() {
    // Warm, organic waveforms with subtle harmonic drift
    m_frameCount = 8;
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* data = m_wavetable.data() + frame * SAMPLES_PER_FRAME;
        float t = static_cast<float>(frame) / static_cast<float>(m_frameCount - 1);

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);

            // Additive synthesis with analog-style harmonics
            float sample = 0.0f;
            int numHarmonics = 3 + static_cast<int>(t * 12);  // More harmonics as we morph

            for (int h = 1; h <= numHarmonics; ++h) {
                // Odd harmonics slightly louder (analog character)
                float amp = 1.0f / static_cast<float>(h);
                if (h % 2 == 1) amp *= 1.2f;

                // Slight phase drift for analog warmth
                float drift = std::sin(static_cast<float>(frame * h) * 0.1f) * 0.02f;
                sample += amp * std::sin((phase + drift) * TWO_PI * static_cast<float>(h));
            }

            // Soft saturation
            sample = std::tanh(sample * 0.8f);
            data[i] = sample;
        }
    }
}

void WavetableSynth::generateDigitalTable() {
    // Harsh, FM-like timbres
    m_frameCount = 8;
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* data = m_wavetable.data() + frame * SAMPLES_PER_FRAME;
        float t = static_cast<float>(frame) / static_cast<float>(m_frameCount - 1);

        // FM modulation index increases with position
        float modIndex = t * 8.0f;
        float ratio = 1.0f + std::floor(t * 4.0f);  // 1:1, 2:1, 3:1, etc.

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);

            // Simple FM: carrier + modulator
            float modulator = std::sin(phase * TWO_PI * ratio);
            float sample = std::sin(phase * TWO_PI + modulator * modIndex);

            data[i] = sample;
        }
    }
}

void WavetableSynth::generateVocalTable() {
    // Formant-based vowel sounds: A -> E -> I -> O -> U
    m_frameCount = 5;  // One frame per vowel
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    // Formant frequencies (Hz) for vowels at ~120Hz fundamental
    // [F1, F2, F3] for each vowel
    const float formants[5][3] = {
        {800.0f, 1150.0f, 2800.0f},   // A
        {400.0f, 2000.0f, 2550.0f},   // E
        {350.0f, 2700.0f, 2900.0f},   // I
        {450.0f, 800.0f,  2830.0f},   // O
        {325.0f, 700.0f,  2530.0f}    // U
    };

    // Approximate formant amplitudes
    const float amps[5][3] = {
        {1.0f, 0.6f, 0.2f},   // A
        {1.0f, 0.4f, 0.3f},   // E
        {1.0f, 0.2f, 0.3f},   // I
        {1.0f, 0.8f, 0.1f},   // O
        {1.0f, 0.8f, 0.1f}    // U
    };

    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* data = m_wavetable.data() + frame * SAMPLES_PER_FRAME;

        // Generate impulse train modulated by formant filters
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);
            float sample = 0.0f;

            // Approximate formants with additive synthesis
            // Use harmonic series that emphasizes formant regions
            float fundamental = 120.0f;  // Reference frequency

            for (int h = 1; h <= 40; ++h) {
                float freq = fundamental * static_cast<float>(h);
                float amp = 0.0f;

                // Sum contributions from each formant
                for (int f = 0; f < 3; ++f) {
                    float formantFreq = formants[frame][f];
                    float formantAmp = amps[frame][f];
                    float bandwidth = 80.0f + static_cast<float>(f) * 40.0f;

                    // Gaussian envelope around formant
                    float dist = (freq - formantFreq) / bandwidth;
                    amp += formantAmp * std::exp(-dist * dist * 0.5f);
                }

                sample += amp * std::sin(phase * TWO_PI * static_cast<float>(h));
            }

            // Normalize
            data[i] = std::tanh(sample * 0.3f);
        }
    }
}

void WavetableSynth::generateTextureTable() {
    // Noise-based, granular textures
    m_frameCount = 8;
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    // Simple pseudo-random for deterministic results
    uint32_t seed = 12345;
    auto randFloat = [&seed]() -> float {
        seed = seed * 1103515245 + 12345;
        return (static_cast<float>(seed & 0x7FFFFFFF) / static_cast<float>(0x7FFFFFFF)) * 2.0f - 1.0f;
    };

    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* data = m_wavetable.data() + frame * SAMPLES_PER_FRAME;
        float t = static_cast<float>(frame) / static_cast<float>(m_frameCount - 1);

        // Mix of filtered noise and harmonics
        float noiseAmount = t;  // More noise as position increases

        // Generate base harmonic content
        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);

            // Harmonic base
            float harmonic = std::sin(phase * TWO_PI);
            harmonic += 0.5f * std::sin(phase * TWO_PI * 2.0f);
            harmonic += 0.25f * std::sin(phase * TWO_PI * 3.0f);
            harmonic *= 0.5f;

            // Noise component
            float noise = randFloat();

            // Mix
            data[i] = harmonic * (1.0f - noiseAmount) + noise * noiseAmount;
        }

        // Simple low-pass smoothing
        for (int pass = 0; pass < 3; ++pass) {
            for (uint32_t i = 1; i < SAMPLES_PER_FRAME - 1; ++i) {
                data[i] = data[i] * 0.5f + (data[i-1] + data[i+1]) * 0.25f;
            }
        }
    }
}

void WavetableSynth::generatePWMTable() {
    // Pulse width modulation from 10% to 90%
    m_frameCount = 8;
    m_wavetable.resize(m_frameCount * SAMPLES_PER_FRAME, 0.0f);

    for (uint32_t frame = 0; frame < m_frameCount; ++frame) {
        float* data = m_wavetable.data() + frame * SAMPLES_PER_FRAME;
        float t = static_cast<float>(frame) / static_cast<float>(m_frameCount - 1);

        // Pulse width from 0.1 to 0.9
        float pulseWidth = 0.1f + t * 0.8f;

        for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(SAMPLES_PER_FRAME);

            // Hard pulse
            data[i] = phase < pulseWidth ? 1.0f : -1.0f;
        }

        // Apply band-limiting to reduce aliasing
        // Simple averaging filter
        for (int pass = 0; pass < 2; ++pass) {
            float prev = data[SAMPLES_PER_FRAME - 1];
            for (uint32_t i = 0; i < SAMPLES_PER_FRAME; ++i) {
                float next = data[(i + 1) % SAMPLES_PER_FRAME];
                float smoothed = data[i] * 0.7f + (prev + next) * 0.15f;
                prev = data[i];
                data[i] = smoothed;
            }
        }
    }
}

// =============================================================================
// Voice Management
// =============================================================================

int WavetableSynth::noteOn(float hz, float vel) {
    int numUnison = static_cast<int>(unisonVoices);
    float spreadCents = static_cast<float>(unisonSpread);
    float stereoWidth = static_cast<float>(unisonStereo);
    float portaMs = static_cast<float>(portamento);

    // Create a new unison group
    uint64_t groupId = ++m_unisonGroupCounter;
    int voicesSpawned = 0;

    for (int u = 0; u < numUnison; ++u) {
        int voiceIdx = findFreeVoice();
        if (voiceIdx < 0) {
            voiceIdx = findVoiceToSteal();
        }
        if (voiceIdx < 0) {
            break;  // No more voices available
        }

        Voice& voice = m_voices[voiceIdx];

        // Calculate unison detune offset
        // Spread voices evenly from -spread/2 to +spread/2
        float detuneOffset = 0.0f;
        if (numUnison > 1) {
            float t = static_cast<float>(u) / static_cast<float>(numUnison - 1);
            detuneOffset = (t - 0.5f) * spreadCents;
        }

        // Calculate stereo pan position
        float pan = 0.0f;
        if (numUnison > 1) {
            float t = static_cast<float>(u) / static_cast<float>(numUnison - 1);
            pan = (t - 0.5f) * 2.0f * stereoWidth;  // -1 to +1
        }

        voice.frequency = hz;
        voice.detuneOffset = detuneOffset;
        voice.pan = pan;
        voice.velocity = std::clamp(vel, 0.0f, 1.0f);
        voice.unisonGroup = groupId;

        // Portamento: start from last played frequency
        if (portaMs > 0.0f && m_lastFrequency > 0.0f) {
            voice.currentFrequency = m_lastFrequency;
            voice.targetFrequency = hz;
        } else {
            voice.currentFrequency = hz;
            voice.targetFrequency = hz;
        }

        voice.phase = 0.0f;
        voice.subPhase = 0.0f;
        voice.lastSample = 0.0f;

        // Amplitude envelope
        voice.envStage = EnvelopeStage::Attack;
        voice.envValue = 0.0f;
        voice.envProgress = 0.0f;

        // Filter envelope
        voice.filterEnvStage = EnvelopeStage::Attack;
        voice.filterEnvValue = 0.0f;
        voice.filterEnvProgress = 0.0f;
        voice.resetFilter();

        voice.noteId = ++m_noteCounter;

        ++voicesSpawned;
    }

    if (voicesSpawned > 0) {
        m_lastFrequency = hz;
    }

    return voicesSpawned;
}

void WavetableSynth::noteOff(float hz) {
    // Find and release all voices with this base frequency (handles unison)
    std::vector<int> voiceIndices = findVoicesByBaseFrequency(hz);
    for (int voiceIdx : voiceIndices) {
        Voice& voice = m_voices[voiceIdx];
        if (voice.envStage != EnvelopeStage::Idle &&
            voice.envStage != EnvelopeStage::Release) {
            // Release amplitude envelope
            voice.envStage = EnvelopeStage::Release;
            voice.envProgress = 0.0f;
            voice.releaseStartValue = voice.envValue;

            // Release filter envelope
            voice.filterEnvStage = EnvelopeStage::Release;
            voice.filterEnvProgress = 0.0f;
            voice.filterReleaseStartValue = voice.filterEnvValue;
        }
    }
}

int WavetableSynth::noteOnMidi(int midiNote, int vel) {
    float velocity = static_cast<float>(std::clamp(vel, 0, 127)) / 127.0f;
    return noteOn(midiToFreq(midiNote), velocity);
}

void WavetableSynth::noteOffMidi(int midiNote) {
    noteOff(midiToFreq(midiNote));
}

void WavetableSynth::allNotesOff() {
    for (auto& voice : m_voices) {
        if (voice.envStage != EnvelopeStage::Idle &&
            voice.envStage != EnvelopeStage::Release) {
            voice.envStage = EnvelopeStage::Release;
            voice.envProgress = 0.0f;
            voice.releaseStartValue = voice.envValue;

            voice.filterEnvStage = EnvelopeStage::Release;
            voice.filterEnvProgress = 0.0f;
            voice.filterReleaseStartValue = voice.filterEnvValue;
        }
    }
}

void WavetableSynth::panic() {
    for (auto& voice : m_voices) {
        voice.envStage = EnvelopeStage::Idle;
        voice.envValue = 0.0f;
        voice.filterEnvStage = EnvelopeStage::Idle;
        voice.filterEnvValue = 0.0f;
        voice.resetFilter();
        voice.frequency = 0.0f;
    }
}

int WavetableSynth::activeVoiceCount() const {
    int count = 0;
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive()) ++count;
    }
    return count;
}

int WavetableSynth::findFreeVoice() const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (!m_voices[i].isActive()) return i;
    }
    return -1;
}

int WavetableSynth::findVoiceToSteal() const {
    // Steal oldest voice
    int max = static_cast<int>(maxVoices);
    int stealIdx = -1;
    uint64_t oldestId = std::numeric_limits<uint64_t>::max();

    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() && m_voices[i].noteId < oldestId) {
            oldestId = m_voices[i].noteId;
            stealIdx = i;
        }
    }
    return stealIdx;
}

int WavetableSynth::findVoiceByFrequency(float hz) const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() &&
            !m_voices[i].isReleasing() &&
            std::abs(m_voices[i].frequency - hz) < FREQ_TOLERANCE) {
            return i;
        }
    }
    return -1;
}

std::vector<int> WavetableSynth::findVoicesByBaseFrequency(float hz) const {
    std::vector<int> result;
    for (int i = 0; i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() &&
            !m_voices[i].isReleasing() &&
            std::abs(m_voices[i].frequency - hz) < FREQ_TOLERANCE) {
            result.push_back(i);
        }
    }
    return result;
}

// =============================================================================
// Sample Generation
// =============================================================================

float WavetableSynth::sampleWavetable(float phase, float pos) const {
    if (m_wavetable.empty() || m_frameCount == 0) return 0.0f;

    // Clamp position to valid range
    pos = std::clamp(pos, 0.0f, 1.0f);

    // Calculate frame indices for interpolation
    float framePos = pos * static_cast<float>(m_frameCount - 1);
    uint32_t frame0 = static_cast<uint32_t>(framePos);
    uint32_t frame1 = std::min(frame0 + 1, m_frameCount - 1);
    float frameFrac = framePos - static_cast<float>(frame0);

    // Wrap phase to 0-1
    phase = phase - std::floor(phase);

    // Calculate sample indices for interpolation within frame
    float samplePos = phase * static_cast<float>(SAMPLES_PER_FRAME);
    uint32_t sample0 = static_cast<uint32_t>(samplePos) % SAMPLES_PER_FRAME;
    uint32_t sample1 = (sample0 + 1) % SAMPLES_PER_FRAME;
    float sampleFrac = samplePos - std::floor(samplePos);

    // Get samples from both frames
    const float* data0 = m_wavetable.data() + frame0 * SAMPLES_PER_FRAME;
    const float* data1 = m_wavetable.data() + frame1 * SAMPLES_PER_FRAME;

    // Bilinear interpolation
    float s0 = linearInterpolate(data0[sample0], data0[sample1], sampleFrac);
    float s1 = linearInterpolate(data1[sample0], data1[sample1], sampleFrac);

    return linearInterpolate(s0, s1, frameFrac);
}

float WavetableSynth::linearInterpolate(float a, float b, float t) const {
    return a + (b - a) * t;
}

float WavetableSynth::centsToRatio(float cents) const {
    return std::pow(2.0f, cents / 1200.0f);
}

float WavetableSynth::warpPhase(float phase, float amount, float lastSample) const {
    if (amount <= 0.0f || m_warpMode == WarpMode::None) {
        return phase;
    }

    // Ensure phase is 0-1
    phase = phase - std::floor(phase);

    switch (m_warpMode) {
        case WarpMode::None:
            return phase;

        case WarpMode::Sync: {
            // Hard sync: multiply frequency by sync ratio
            // Higher amount = higher sync frequency
            float syncRatio = 1.0f + amount * 7.0f;  // 1x to 8x
            float syncPhase = phase * syncRatio;
            return syncPhase - std::floor(syncPhase);
        }

        case WarpMode::BendPlus: {
            // Phase bend up: emphasizes attack portion
            // Uses power function to compress/expand phase
            float exponent = 1.0f + amount * 3.0f;  // 1 to 4
            return std::pow(phase, exponent);
        }

        case WarpMode::BendMinus: {
            // Phase bend down: softer, rounder
            float exponent = 1.0f / (1.0f + amount * 3.0f);  // 1 to 0.25
            return std::pow(phase, exponent);
        }

        case WarpMode::Mirror: {
            // Mirror at midpoint - creates symmetrical waveform
            float midpoint = 0.5f - amount * 0.3f;  // Adjustable mirror point
            if (phase > midpoint) {
                return midpoint - (phase - midpoint);
            }
            return phase / midpoint * 0.5f;  // Scale first half
        }

        case WarpMode::Asym: {
            // Asymmetric: stretch positive half, compress negative
            // Creates odd-harmonic emphasis
            if (phase < 0.5f) {
                // First half: stretch based on amount
                float stretch = 0.5f + amount * 0.3f;
                return (phase / 0.5f) * stretch;
            } else {
                // Second half: compress
                float stretch = 0.5f + amount * 0.3f;
                return stretch + ((phase - 0.5f) / 0.5f) * (1.0f - stretch);
            }
        }

        case WarpMode::Quantize: {
            // Bit-reduce phase for lo-fi stepped effect
            int steps = static_cast<int>(256.0f - amount * 252.0f);  // 256 to 4 steps
            steps = std::max(4, steps);
            return std::floor(phase * static_cast<float>(steps)) / static_cast<float>(steps);
        }

        case WarpMode::FM: {
            // Self-FM: phase modulated by previous output
            // Creates increasingly complex harmonics
            float modDepth = amount * 0.5f;  // Max 0.5 phase deviation
            float modPhase = phase + lastSample * modDepth;
            return modPhase - std::floor(modPhase);
        }

        case WarpMode::Flip: {
            // Flip second half for octave-up harmonic content
            if (phase >= 0.5f) {
                // Blend between normal and flipped based on amount
                float flipped = 1.0f - phase;
                return phase * (1.0f - amount) + flipped * amount;
            }
            return phase;
        }
    }

    return phase;
}

// =============================================================================
// Envelope
// =============================================================================

float WavetableSynth::computeEnvelope(Voice& voice) const {
    switch (voice.envStage) {
        case EnvelopeStage::Attack:
            return voice.envProgress;
        case EnvelopeStage::Decay: {
            float s = static_cast<float>(sustain);
            return 1.0f - voice.envProgress * (1.0f - s);
        }
        case EnvelopeStage::Sustain:
            return static_cast<float>(sustain);
        case EnvelopeStage::Release:
            return voice.releaseStartValue * (1.0f - voice.envProgress);
        case EnvelopeStage::Idle:
        default:
            return 0.0f;
    }
}

void WavetableSynth::advanceEnvelope(Voice& voice, uint32_t samples) {
    if (voice.envStage == EnvelopeStage::Idle) return;

    float timeSeconds = static_cast<float>(samples) / static_cast<float>(m_sampleRate);

    switch (voice.envStage) {
        case EnvelopeStage::Attack: {
            float attackTime = std::max(0.001f, static_cast<float>(attack));

            // Velocity to attack modulation:
            // velToAttack > 0: high velocity = shorter attack
            // velToAttack < 0: high velocity = longer attack
            float velAtk = static_cast<float>(velToAttack);
            if (velAtk != 0.0f) {
                float velMod = velAtk * (1.0f - voice.velocity);  // high vel = less mod
                attackTime *= std::pow(2.0f, velMod * 2.0f);  // ±2 octaves range
                attackTime = std::clamp(attackTime, 0.001f, 10.0f);
            }

            voice.envProgress += timeSeconds / attackTime;
            if (voice.envProgress >= 1.0f) {
                voice.envProgress = 0.0f;
                voice.envStage = EnvelopeStage::Decay;
            }
            break;
        }
        case EnvelopeStage::Decay: {
            float decayTime = std::max(0.001f, static_cast<float>(decay));
            voice.envProgress += timeSeconds / decayTime;
            if (voice.envProgress >= 1.0f) {
                voice.envProgress = 0.0f;
                voice.envStage = EnvelopeStage::Sustain;
            }
            break;
        }
        case EnvelopeStage::Sustain:
            // Stay until noteOff
            break;
        case EnvelopeStage::Release: {
            float releaseTime = std::max(0.001f, static_cast<float>(release));
            voice.envProgress += timeSeconds / releaseTime;
            if (voice.envProgress >= 1.0f) {
                voice.envStage = EnvelopeStage::Idle;
                voice.envValue = 0.0f;
            }
            break;
        }
        default:
            break;
    }

    voice.envValue = computeEnvelope(voice);
}

// =============================================================================
// Filter Envelope
// =============================================================================

float WavetableSynth::computeFilterEnvelope(Voice& voice) const {
    switch (voice.filterEnvStage) {
        case EnvelopeStage::Attack:
            return voice.filterEnvProgress;
        case EnvelopeStage::Decay: {
            float s = static_cast<float>(filterSustain);
            return 1.0f - voice.filterEnvProgress * (1.0f - s);
        }
        case EnvelopeStage::Sustain:
            return static_cast<float>(filterSustain);
        case EnvelopeStage::Release:
            return voice.filterReleaseStartValue * (1.0f - voice.filterEnvProgress);
        case EnvelopeStage::Idle:
        default:
            return 0.0f;
    }
}

void WavetableSynth::advanceFilterEnvelope(Voice& voice, uint32_t samples) {
    if (voice.filterEnvStage == EnvelopeStage::Idle) return;

    float timeSeconds = static_cast<float>(samples) / static_cast<float>(m_sampleRate);

    switch (voice.filterEnvStage) {
        case EnvelopeStage::Attack: {
            float attackTime = std::max(0.001f, static_cast<float>(filterAttack));
            voice.filterEnvProgress += timeSeconds / attackTime;
            if (voice.filterEnvProgress >= 1.0f) {
                voice.filterEnvProgress = 0.0f;
                voice.filterEnvStage = EnvelopeStage::Decay;
            }
            break;
        }
        case EnvelopeStage::Decay: {
            float decayTime = std::max(0.001f, static_cast<float>(filterDecay));
            voice.filterEnvProgress += timeSeconds / decayTime;
            if (voice.filterEnvProgress >= 1.0f) {
                voice.filterEnvProgress = 0.0f;
                voice.filterEnvStage = EnvelopeStage::Sustain;
            }
            break;
        }
        case EnvelopeStage::Sustain:
            // Stay until noteOff
            break;
        case EnvelopeStage::Release: {
            float releaseTime = std::max(0.001f, static_cast<float>(filterRelease));
            voice.filterEnvProgress += timeSeconds / releaseTime;
            if (voice.filterEnvProgress >= 1.0f) {
                voice.filterEnvStage = EnvelopeStage::Idle;
                voice.filterEnvValue = 0.0f;
            }
            break;
        }
        default:
            break;
    }

    voice.filterEnvValue = computeFilterEnvelope(voice);
}

// =============================================================================
// Per-Voice Filter (Biquad)
// =============================================================================

float WavetableSynth::applyFilter(Voice& voice, float input, float cutoffHz, float resonance) {
    // Clamp cutoff to valid range
    cutoffHz = std::clamp(cutoffHz, 20.0f, static_cast<float>(m_sampleRate) * 0.45f);
    resonance = std::clamp(resonance, 0.0f, 1.0f);

    // Calculate biquad coefficients
    float omega = TWO_PI * cutoffHz / static_cast<float>(m_sampleRate);
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);

    // Q from resonance (0.5 to 20)
    float Q = 0.5f + resonance * 19.5f;
    float alpha = sinOmega / (2.0f * Q);

    // Calculate coefficients based on filter type
    float b0, b1, b2, a0, a1, a2;

    switch (m_filterType) {
        case SynthFilterType::LP12:
        case SynthFilterType::LP24: {
            // Low-pass
            b0 = (1.0f - cosOmega) / 2.0f;
            b1 = 1.0f - cosOmega;
            b2 = (1.0f - cosOmega) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case SynthFilterType::HP12: {
            // High-pass
            b0 = (1.0f + cosOmega) / 2.0f;
            b1 = -(1.0f + cosOmega);
            b2 = (1.0f + cosOmega) / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case SynthFilterType::BP: {
            // Band-pass (constant skirt gain)
            b0 = sinOmega / 2.0f;
            b1 = 0.0f;
            b2 = -sinOmega / 2.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        case SynthFilterType::Notch: {
            // Notch
            b0 = 1.0f;
            b1 = -2.0f * cosOmega;
            b2 = 1.0f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cosOmega;
            a2 = 1.0f - alpha;
            break;
        }
        default:
            return input;
    }

    // Normalize coefficients
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    // Apply filter using transposed direct form II
    // First stage
    float output = b0 * input + voice.filterZ1[0];
    voice.filterZ1[0] = b1 * input - a1 * output + voice.filterZ2[0];
    voice.filterZ2[0] = b2 * input - a2 * output;

    // For 24dB (4-pole), apply second stage
    if (m_filterType == SynthFilterType::LP24) {
        float input2 = output;
        output = b0 * input2 + voice.filterZ1[1];
        voice.filterZ1[1] = b1 * input2 - a1 * output + voice.filterZ2[1];
        voice.filterZ2[1] = b2 * input2 - a2 * output;
    }

    return output;
}

// =============================================================================
// Audio Generation
// =============================================================================

void WavetableSynth::generateBlock(uint32_t blockFrameCount) {
    if (!m_initialized) return;

    // Resize buffer if needed
    if (m_output.frameCount != blockFrameCount) {
        m_output.resize(blockFrameCount);
    }

    // Clear output
    std::memset(m_output.samples, 0, blockFrameCount * 2 * sizeof(float));

    // Get parameters
    float pos = static_cast<float>(position);
    float vol = static_cast<float>(volume);
    float detuneAmount = static_cast<float>(detune);
    float subLvl = static_cast<float>(subLevel);
    int subOct = static_cast<int>(subOctave);
    float portaMs = static_cast<float>(portamento);
    float velToVol = static_cast<float>(velToVolume);
    float warpAmt = static_cast<float>(warpAmount);
    // Note: velToAttack is applied in advanceEnvelope()

    // Filter parameters
    float baseCutoff = static_cast<float>(filterCutoff);
    float reso = static_cast<float>(filterResonance);
    float keytrack = static_cast<float>(filterKeytrack);
    float envAmt = static_cast<float>(filterEnvAmount);

    // Portamento rate coefficient (per sample)
    float portaRate = 1.0f;  // Instant by default
    if (portaMs > 0.0f) {
        // Exponential glide: higher values = slower glide
        float portaSamples = portaMs * 0.001f * static_cast<float>(m_sampleRate);
        portaRate = 1.0f - std::exp(-4.0f / portaSamples);  // 4 time constants to reach target
    }

    // Sub oscillator frequency divisor
    float subDivisor = (subOct == -2) ? 4.0f : 2.0f;

    // Process each active voice
    int activeCount = 0;

    for (int v = 0; v < static_cast<int>(m_voices.size()); ++v) {
        Voice& voice = m_voices[v];
        if (!voice.isActive()) continue;

        ++activeCount;

        // Apply velocity to volume
        float velVolume = 1.0f - velToVol * (1.0f - voice.velocity);

        for (uint32_t i = 0; i < blockFrameCount; ++i) {
            // Advance envelopes
            advanceEnvelope(voice, 1);
            advanceFilterEnvelope(voice, 1);

            if (voice.envStage == EnvelopeStage::Idle) {
                break;
            }

            // Portamento: glide currentFrequency toward targetFrequency
            if (portaMs > 0.0f && voice.currentFrequency != voice.targetFrequency) {
                voice.currentFrequency += (voice.targetFrequency - voice.currentFrequency) * portaRate;
                // Snap to target when close
                if (std::abs(voice.currentFrequency - voice.targetFrequency) < 0.01f) {
                    voice.currentFrequency = voice.targetFrequency;
                }
            }

            // Calculate final frequency with unison detune + global detune
            float totalDetune = voice.detuneOffset + detuneAmount * 0.5f;
            float freq = voice.currentFrequency * centsToRatio(totalDetune);
            float phaseInc = freq / static_cast<float>(m_sampleRate);

            // Apply phase warp before sampling
            float warpedPhase = warpPhase(voice.phase, warpAmt, voice.lastSample);

            // Sample main wavetable
            float sample = sampleWavetable(warpedPhase, pos);

            // Store sample for FM feedback (used next sample)
            voice.lastSample = sample;

            // Add sub oscillator (simple sine)
            if (subLvl > 0.0f) {
                float subFreq = voice.currentFrequency / subDivisor;
                float subPhaseInc = subFreq / static_cast<float>(m_sampleRate);
                float subSample = std::sin(voice.subPhase * TWO_PI);
                sample = sample * (1.0f - subLvl) + subSample * subLvl;

                // Advance sub phase
                voice.subPhase += subPhaseInc;
                if (voice.subPhase >= 1.0f) voice.subPhase -= 1.0f;
            }

            // Apply per-voice filter
            // Calculate modulated cutoff: base + envelope + keytracking
            float cutoff = baseCutoff;

            // Filter envelope modulation (bipolar: envAmt can be negative)
            // When envAmt > 0: envelope opens filter, when < 0: envelope closes filter
            float envMod = voice.filterEnvValue * envAmt;
            cutoff = cutoff * std::pow(2.0f, envMod * 4.0f);  // ±4 octaves range

            // Keytracking: higher notes = higher cutoff
            // Reference: middle C (MIDI 60) = 261.63 Hz
            if (keytrack > 0.0f) {
                float octavesFromC4 = std::log2(voice.currentFrequency / 261.63f);
                cutoff *= std::pow(2.0f, octavesFromC4 * keytrack);
            }

            // Apply filter (only if cutoff is below Nyquist)
            if (cutoff < static_cast<float>(m_sampleRate) * 0.45f) {
                sample = applyFilter(voice, sample, cutoff, reso);
            }

            // Apply envelope and velocity
            float env = voice.envValue * velVolume;
            sample *= env;

            // Apply panning for unison stereo spread
            float panL = std::min(1.0f, 1.0f - voice.pan);  // pan -1 = full left
            float panR = std::min(1.0f, 1.0f + voice.pan);  // pan +1 = full right

            // Accumulate to output
            m_output.samples[i * 2] += sample * panL;
            m_output.samples[i * 2 + 1] += sample * panR;

            // Advance phase
            voice.phase += phaseInc;
            if (voice.phase >= 1.0f) voice.phase -= 1.0f;
        }
    }

    // Apply volume and normalize by voice count
    float voiceScale = activeCount > 0 ? 1.0f / std::sqrt(static_cast<float>(activeCount)) : 1.0f;
    for (uint32_t i = 0; i < blockFrameCount * 2; ++i) {
        m_output.samples[i] *= vol * voiceScale;
    }
}

} // namespace vivid::audio
