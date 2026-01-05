// Vivid Audio - MultiSampler Implementation

#include <vivid/audio/multi_sampler.h>
#include <vivid/asset_loader.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>
#include <nlohmann/json.hpp>
#include <pugixml.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace vivid::audio {

using json = nlohmann::json;

MultiSampler::MultiSampler() {
    registerParam(volume);
    registerParam(maxVoices);
    registerParam(attack);
    registerParam(decay);
    registerParam(sustain);
    registerParam(release);
    registerParam(velCurve);

    // Pre-allocate maximum voices
    m_voices.resize(64);

    // Create default group
    m_groups.push_back(SampleGroup{"Default", {}, -1, -1, -1, -1, 0.0f, -1});
}

void MultiSampler::init(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    allocateOutput();
    m_initialized = true;

    // Load pending preset if set before init
    if (!m_pendingPreset.empty()) {
        loadPreset(m_pendingPreset);
        m_pendingPreset.clear();
    }
}

void MultiSampler::process(Context& ctx) {
    if (!m_initialized) return;
    // Audio generation happens in generateBlock()
}

void MultiSampler::cleanup() {
    m_voices.clear();
    m_groups.clear();
    m_roundRobinIndex.clear();
    releaseOutput();
    m_initialized = false;
}

bool MultiSampler::loadPreset(const std::string& jsonPath) {
    // If not initialized yet, store path for later
    if (!m_initialized) {
        m_pendingPreset = jsonPath;
        return true;
    }

    // Resolve path
    auto resolved = AssetLoader::instance().resolve(jsonPath);
    std::string loadPath = resolved.empty() ? jsonPath : resolved.string();

    std::ifstream file(loadPath);
    if (!file.is_open()) {
        return false;
    }

    json preset;
    try {
        file >> preset;
    } catch (...) {
        return false;
    }

    // Store base path for resolving sample paths
    m_basePath = std::filesystem::path(loadPath).parent_path().string();

    // Clear existing data
    clear();

    // Helper to load samples into a group
    auto loadSamplesIntoGroup = [this](SampleGroup& group, const json& samples) {
        for (auto& s : samples) {
            SampleRegion region;
            region.path = s.value("path", "");
            region.rootNote = s.value("root_note", 60);
            region.loNote = s.value("lo_note", region.rootNote);
            region.hiNote = s.value("hi_note", region.rootNote);
            region.loVel = s.value("lo_vel", 0);
            region.hiVel = s.value("hi_vel", 127);
            region.volumeDb = s.value("volume_db", 0.0f);
            region.pan = s.value("pan", 0.0f);
            region.tuneCents = s.value("tune_cents", 0);
            region.loopEnabled = s.value("loop_enabled", false);

            if (s.contains("loop_start")) {
                region.loopStart = s["loop_start"].get<uint64_t>();
            }
            if (s.contains("loop_end")) {
                region.loopEnd = s["loop_end"].get<uint64_t>();
            }

            // Normalize path separators (Windows backslashes to forward slashes)
            std::string normalizedPath = region.path;
            std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

            // Resolve path relative to preset
            std::filesystem::path fullPath = std::filesystem::path(m_basePath) / normalizedPath;
            region.path = fullPath.string();

            group.regions.push_back(std::move(region));
        }
    };

    // Check for multi-group format with keyswitches
    if (preset.contains("groups")) {
        for (auto& g : preset["groups"]) {
            SampleGroup group;
            group.name = g.value("name", "Group");
            group.keyswitch = g.value("keyswitch", -1);
            group.volumeDb = g.value("volume_db", 0.0f);

            // Load group envelope
            if (g.contains("envelope")) {
                auto& env = g["envelope"];
                group.attack = env.value("attack", -1.0f);
                group.decay = env.value("decay", -1.0f);
                group.sustain = env.value("sustain", -1.0f);
                group.release = env.value("release", -1.0f);
            }

            // Load samples
            if (g.contains("samples")) {
                loadSamplesIntoGroup(group, g["samples"]);
            }

            m_groups.push_back(std::move(group));
        }
    }
    // Single group format (backward compatible)
    else if (preset.contains("samples")) {
        SampleGroup group;
        group.name = preset.value("name", "Preset");

        // Load envelope settings
        if (preset.contains("envelope")) {
            auto& env = preset["envelope"];
            group.attack = env.value("attack", -1.0f);
            group.decay = env.value("decay", -1.0f);
            group.sustain = env.value("sustain", -1.0f);
            group.release = env.value("release", -1.0f);
        }

        loadSamplesIntoGroup(group, preset["samples"]);
        m_groups.push_back(std::move(group));
    }

    m_activeGroup = 0;
    return true;
}

bool MultiSampler::loadDspreset(const std::string& dspresetPath) {
    // If not initialized yet, store path for later
    if (!m_initialized) {
        m_pendingPreset = dspresetPath;
        return true;
    }

    // Resolve path
    auto resolved = AssetLoader::instance().resolve(dspresetPath);
    std::string loadPath = resolved.empty() ? dspresetPath : resolved.string();

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_file(loadPath.c_str());
    if (!result) {
        return false;
    }

    // Store base path for resolving sample paths
    m_basePath = std::filesystem::path(loadPath).parent_path().string();

    // Clear existing data
    clear();

    // Parse <DecentSampler> root
    auto root = doc.child("DecentSampler");
    if (!root) {
        return false;
    }

    // Helper to parse time strings like "100ms", "1.5s", or plain numbers
    auto parseTime = [](const char* str) -> float {
        if (!str || str[0] == '\0') return 0.0f;
        std::string s(str);
        if (s.find("ms") != std::string::npos) {
            return std::stof(s) / 1000.0f;
        }
        if (s.find("s") != std::string::npos) {
            size_t pos = s.find("s");
            return std::stof(s.substr(0, pos));
        }
        return std::stof(s);
    };

    // Helper to parse dB strings like "-3dB", "0dB"
    auto parseDb = [](const char* str) -> float {
        if (!str || str[0] == '\0') return 0.0f;
        std::string s(str);
        size_t pos = s.find("dB");
        if (pos != std::string::npos) {
            s = s.substr(0, pos);
        }
        pos = s.find("db");
        if (pos != std::string::npos) {
            s = s.substr(0, pos);
        }
        try {
            return std::stof(s);
        } catch (...) {
            return 0.0f;
        }
    };

    // Parse <groups> element
    for (auto groups : root.children("groups")) {
        // Groups-level envelope defaults
        float groupsAttack = parseTime(groups.attribute("attack").value());
        float groupsDecay = parseTime(groups.attribute("decay").value());
        float groupsSustain = groups.attribute("sustain").as_float(1.0f);
        float groupsRelease = parseTime(groups.attribute("release").value());
        float groupsVolume = parseDb(groups.attribute("volume").value());

        // Parse each <group>
        for (auto group : groups.children("group")) {
            SampleGroup sg;
            sg.name = group.attribute("name").as_string("Group");
            sg.volumeDb = parseDb(group.attribute("volume").value()) + groupsVolume;

            // Group envelope (inherit from groups if not specified)
            sg.attack = group.attribute("attack") ? parseTime(group.attribute("attack").value()) : groupsAttack;
            sg.decay = group.attribute("decay") ? parseTime(group.attribute("decay").value()) : groupsDecay;
            sg.sustain = group.attribute("sustain") ? group.attribute("sustain").as_float(1.0f) : groupsSustain;
            sg.release = group.attribute("release") ? parseTime(group.attribute("release").value()) : groupsRelease;

            // Keyswitch (if present)
            sg.keyswitch = group.attribute("keyswitch").as_int(-1);

            // Parse each <sample>
            for (auto sample : group.children("sample")) {
                SampleRegion region;
                region.path = sample.attribute("path").as_string("");
                region.rootNote = sample.attribute("rootNote").as_int(60);
                region.loNote = sample.attribute("loNote").as_int(region.rootNote);
                region.hiNote = sample.attribute("hiNote").as_int(region.rootNote);
                region.loVel = sample.attribute("loVel").as_int(0);
                region.hiVel = sample.attribute("hiVel").as_int(127);
                region.volumeDb = parseDb(sample.attribute("volume").value());
                region.pan = sample.attribute("pan").as_float(0.0f);
                region.tuneCents = sample.attribute("tuning").as_int(0);

                // Loop settings
                region.loopEnabled = std::string(sample.attribute("loopEnabled").as_string("false")) == "true";
                region.loopStart = sample.attribute("loopStart").as_ullong(0);
                region.loopEnd = sample.attribute("loopEnd").as_ullong(0);
                region.loopCrossfade = sample.attribute("loopCrossfade").as_ullong(0);

                // Normalize path separators
                std::string normalizedPath = region.path;
                std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

                // Resolve path relative to preset
                std::filesystem::path fullPath = std::filesystem::path(m_basePath) / normalizedPath;
                region.path = fullPath.string();

                sg.regions.push_back(std::move(region));
            }

            if (!sg.regions.empty()) {
                m_groups.push_back(std::move(sg));
            }
        }
    }

    // If no groups were loaded, create empty default group
    if (m_groups.empty()) {
        m_groups.push_back(SampleGroup{"Default", {}, -1, -1, -1, -1, 0.0f, -1});
    }

    m_activeGroup = 0;
    return true;
}

void MultiSampler::addRegion(const SampleRegion& region) {
    if (m_groups.empty()) {
        m_groups.push_back(SampleGroup{"Default", {}, -1, -1, -1, -1, 0.0f, -1});
    }
    m_groups[0].regions.push_back(region);
}

void MultiSampler::addGroup(const SampleGroup& group) {
    m_groups.push_back(group);
}

void MultiSampler::clear() {
    m_groups.clear();
    m_roundRobinIndex.clear();
    m_activeGroup = 0;
    panic();
}

int MultiSampler::regionCount() const {
    int count = 0;
    for (const auto& group : m_groups) {
        count += static_cast<int>(group.regions.size());
    }
    return count;
}

SampleRegion* MultiSampler::findRegion(int note, int velocity) {
    if (m_activeGroup < 0 || m_activeGroup >= static_cast<int>(m_groups.size())) {
        return nullptr;
    }

    auto& group = m_groups[m_activeGroup];
    int vel = static_cast<int>(velocity * 127.0f);

    // Find matching regions
    std::vector<SampleRegion*> matches;
    for (auto& region : group.regions) {
        if (note >= region.loNote && note <= region.hiNote &&
            vel >= region.loVel && vel <= region.hiVel) {
            matches.push_back(&region);
        }
    }

    if (!matches.empty()) {
        // Round-robin selection
        int& rrIndex = m_roundRobinIndex[note];
        rrIndex = (rrIndex + 1) % static_cast<int>(matches.size());
        return matches[rrIndex];
    }

    // Fallback: find closest by note, ignore velocity
    SampleRegion* closest = nullptr;
    int closestDist = std::numeric_limits<int>::max();

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

int MultiSampler::noteOn(int midiNote, float velocity) {
    // Check for keyswitch first
    for (size_t i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].keyswitch == midiNote) {
            m_activeGroup = static_cast<int>(i);
            return -1;  // Keyswitch consumed, no voice
        }
    }

    // Find matching region
    SampleRegion* region = findRegion(midiNote, velocity);
    if (!region) {
        return -1;
    }

    // Ensure sample is loaded
    if (!ensureLoaded(*region)) {
        return -1;
    }

    // Find a voice
    int voiceIdx = findFreeVoice();
    if (voiceIdx < 0) {
        voiceIdx = findVoiceToSteal();
    }
    if (voiceIdx < 0) {
        return -1;
    }

    // Get envelope settings from group or global
    const auto& group = m_groups[m_activeGroup];

    Voice& voice = m_voices[voiceIdx];
    voice.midiNote = midiNote;
    voice.region = region;
    voice.position = 0.0;
    voice.pitch = pitchFromNote(midiNote, region->rootNote, region->tuneCents);
    voice.velocity = applyVelocityCurve(velocity);
    voice.pan = region->pan;
    voice.volumeScale = dbToLinear(region->volumeDb + group.volumeDb);

    // Resolve envelope times
    voice.envAttack = (group.attack >= 0) ? group.attack : static_cast<float>(attack);
    voice.envDecay = (group.decay >= 0) ? group.decay : static_cast<float>(decay);
    voice.envSustain = (group.sustain >= 0) ? group.sustain : static_cast<float>(sustain);
    voice.envRelease = (group.release >= 0) ? group.release : static_cast<float>(release);

    voice.envStage = EnvelopeStage::Attack;
    voice.envValue = 0.0f;
    voice.envProgress = 0.0f;
    voice.noteId = ++m_noteCounter;

    return voiceIdx;
}

void MultiSampler::noteOff(int midiNote) {
    int voiceIdx = findVoiceByNote(midiNote);
    if (voiceIdx >= 0) {
        Voice& voice = m_voices[voiceIdx];
        if (voice.envStage != EnvelopeStage::Idle &&
            voice.envStage != EnvelopeStage::Release) {
            voice.envStage = EnvelopeStage::Release;
            voice.envProgress = 0.0f;
            voice.releaseStartValue = voice.envValue;
        }
    }
}

void MultiSampler::allNotesOff() {
    for (auto& voice : m_voices) {
        if (voice.envStage != EnvelopeStage::Idle &&
            voice.envStage != EnvelopeStage::Release) {
            voice.envStage = EnvelopeStage::Release;
            voice.envProgress = 0.0f;
            voice.releaseStartValue = voice.envValue;
        }
    }
}

void MultiSampler::panic() {
    for (auto& voice : m_voices) {
        voice.envStage = EnvelopeStage::Idle;
        voice.envValue = 0.0f;
        voice.midiNote = -1;
        voice.region = nullptr;
    }
}

void MultiSampler::setKeyswitch(int note) {
    for (size_t i = 0; i < m_groups.size(); ++i) {
        if (m_groups[i].keyswitch == note) {
            m_activeGroup = static_cast<int>(i);
            return;
        }
    }
}

void MultiSampler::setActiveGroup(int index) {
    if (index >= 0 && index < static_cast<int>(m_groups.size())) {
        m_activeGroup = index;
    }
}

int MultiSampler::activeVoiceCount() const {
    int count = 0;
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive()) {
            ++count;
        }
    }
    return count;
}

int MultiSampler::findFreeVoice() const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (!m_voices[i].isActive()) {
            return i;
        }
    }
    return -1;
}

int MultiSampler::findVoiceToSteal() const {
    int max = static_cast<int>(maxVoices);
    int stealIdx = -1;
    uint64_t oldestId = std::numeric_limits<uint64_t>::max();

    // Steal oldest voice
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() && m_voices[i].noteId < oldestId) {
            oldestId = m_voices[i].noteId;
            stealIdx = i;
        }
    }

    return stealIdx;
}

int MultiSampler::findVoiceByNote(int midiNote) const {
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        if (m_voices[i].isActive() &&
            !m_voices[i].isReleasing() &&
            m_voices[i].midiNote == midiNote) {
            return i;
        }
    }
    return -1;
}

float MultiSampler::computeEnvelope(const Voice& voice) const {
    switch (voice.envStage) {
        case EnvelopeStage::Attack:
            return voice.envProgress;

        case EnvelopeStage::Decay:
            return 1.0f - voice.envProgress * (1.0f - voice.envSustain);

        case EnvelopeStage::Sustain:
            return voice.envSustain;

        case EnvelopeStage::Release:
            return voice.releaseStartValue * (1.0f - voice.envProgress);

        case EnvelopeStage::Idle:
        default:
            return 0.0f;
    }
}

void MultiSampler::advanceEnvelope(Voice& voice, uint32_t samples) {
    if (voice.envStage == EnvelopeStage::Idle) return;

    float timeSeconds = static_cast<float>(samples) / static_cast<float>(m_sampleRate);

    switch (voice.envStage) {
        case EnvelopeStage::Attack: {
            float attackTime = std::max(0.001f, voice.envAttack);
            voice.envProgress += timeSeconds / attackTime;
            if (voice.envProgress >= 1.0f) {
                voice.envProgress = 0.0f;
                voice.envStage = EnvelopeStage::Decay;
            }
            break;
        }

        case EnvelopeStage::Decay: {
            float decayTime = std::max(0.001f, voice.envDecay);
            voice.envProgress += timeSeconds / decayTime;
            if (voice.envProgress >= 1.0f) {
                voice.envProgress = 0.0f;
                voice.envStage = EnvelopeStage::Sustain;
            }
            break;
        }

        case EnvelopeStage::Sustain:
            // Stay in sustain until noteOff
            break;

        case EnvelopeStage::Release: {
            float releaseTime = std::max(0.001f, voice.envRelease);
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

float MultiSampler::sampleAt(const SampleRegion& region, double position, int channel) const {
    if (region.samples.empty()) return 0.0f;

    size_t pos0 = static_cast<size_t>(position);
    size_t pos1 = pos0 + 1;

    // Bounds check
    if (pos0 >= region.sampleFrames) return 0.0f;
    if (pos1 >= region.sampleFrames) pos1 = pos0;

    float frac = static_cast<float>(position - static_cast<double>(pos0));

    // Interleaved stereo: [L0, R0, L1, R1, ...]
    float s0 = region.samples[pos0 * 2 + channel];
    float s1 = region.samples[pos1 * 2 + channel];

    // Linear interpolation
    return s0 + frac * (s1 - s0);
}

void MultiSampler::processVoice(Voice& voice, float* outputL, float* outputR, uint32_t frames) {
    if (!voice.isActive() || !voice.region) return;

    const auto& region = *voice.region;

    for (uint32_t i = 0; i < frames; ++i) {
        // Update envelope per sample
        advanceEnvelope(voice, 1);

        if (voice.envStage == EnvelopeStage::Idle) {
            break;  // Voice became inactive
        }

        // Check if we've reached end of sample
        if (voice.position >= static_cast<double>(region.sampleFrames)) {
            if (region.loopEnabled) {
                // Loop back
                uint64_t loopEnd = (region.loopEnd > 0) ? region.loopEnd : region.sampleFrames;
                uint64_t loopLen = loopEnd - region.loopStart;
                if (loopLen > 0) {
                    voice.position = static_cast<double>(region.loopStart) +
                        std::fmod(voice.position - static_cast<double>(region.loopStart),
                                  static_cast<double>(loopLen));
                }
            } else {
                // End of sample, release
                if (voice.envStage != EnvelopeStage::Release) {
                    voice.envStage = EnvelopeStage::Release;
                    voice.envProgress = 0.0f;
                    voice.releaseStartValue = voice.envValue;
                }
                voice.position = static_cast<double>(region.sampleFrames);
            }
        }

        float env = voice.envValue * voice.velocity * voice.volumeScale;
        float sampleL = sampleAt(region, voice.position, 0) * env;
        float sampleR = sampleAt(region, voice.position, 1) * env;

        // Apply pan
        float panL = 1.0f - std::max(0.0f, voice.pan);
        float panR = 1.0f + std::min(0.0f, voice.pan);

        outputL[i] += sampleL * panL;
        outputR[i] += sampleR * panR;

        // Advance position by pitch
        voice.position += static_cast<double>(voice.pitch);
    }
}

void MultiSampler::generateBlock(uint32_t frameCount) {
    if (!m_initialized) return;

    // Resize buffer if needed
    if (m_output.frameCount != frameCount) {
        m_output.resize(frameCount);
    }

    // Temporary buffers for mixing
    std::vector<float> mixL(frameCount, 0.0f);
    std::vector<float> mixR(frameCount, 0.0f);

    // Process all active voices
    int max = static_cast<int>(maxVoices);
    for (int i = 0; i < max && i < static_cast<int>(m_voices.size()); ++i) {
        processVoice(m_voices[i], mixL.data(), mixR.data(), frameCount);
    }

    // Apply master volume and interleave to output
    float vol = static_cast<float>(volume);
    // Normalize by voice count to prevent clipping
    float voiceScale = 1.0f / std::sqrt(static_cast<float>(std::max(1, max)));

    for (uint32_t i = 0; i < frameCount; ++i) {
        m_output.samples[i * 2] = mixL[i] * vol * voiceScale;
        m_output.samples[i * 2 + 1] = mixR[i] * vol * voiceScale;
    }
}

bool MultiSampler::ensureLoaded(SampleRegion& region) {
    if (region.loaded) return true;
    return loadWAV(region.path, region);
}

bool MultiSampler::loadWAV(const std::string& path, SampleRegion& region) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read RIFF header
    char riff[4];
    file.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        return false;
    }

    uint32_t fileSize;
    file.read(reinterpret_cast<char*>(&fileSize), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        return false;
    }

    // Find fmt and data chunks
    uint16_t audioFormat = 0;
    uint16_t numChannels = 0;
    uint32_t fileSampleRate = 0;
    uint16_t bitsPerSample = 0;
    std::vector<float> rawSamples;

    while (file.good()) {
        char chunkId[4];
        uint32_t chunkSize;

        file.read(chunkId, 4);
        if (!file.good()) break;

        file.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (!file.good()) break;

        if (std::strncmp(chunkId, "fmt ", 4) == 0) {
            file.read(reinterpret_cast<char*>(&audioFormat), 2);
            file.read(reinterpret_cast<char*>(&numChannels), 2);
            file.read(reinterpret_cast<char*>(&fileSampleRate), 4);
            file.seekg(6, std::ios::cur);  // Skip byteRate and blockAlign
            file.read(reinterpret_cast<char*>(&bitsPerSample), 2);

            // Skip extra format bytes if present
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::strncmp(chunkId, "data", 4) == 0) {
            // Read sample data
            uint32_t numSamples = chunkSize / (bitsPerSample / 8);
            rawSamples.resize(numSamples);

            if (bitsPerSample == 16) {
                std::vector<int16_t> buffer(numSamples);
                file.read(reinterpret_cast<char*>(buffer.data()), chunkSize);
                for (uint32_t i = 0; i < numSamples; ++i) {
                    rawSamples[i] = static_cast<float>(buffer[i]) / 32768.0f;
                }
            } else if (bitsPerSample == 24) {
                for (uint32_t i = 0; i < numSamples; ++i) {
                    uint8_t bytes[3];
                    file.read(reinterpret_cast<char*>(bytes), 3);
                    int32_t value = (bytes[2] << 16) | (bytes[1] << 8) | bytes[0];
                    if (value & 0x800000) value |= 0xFF000000;  // Sign extend
                    rawSamples[i] = static_cast<float>(value) / 8388608.0f;
                }
            } else if (bitsPerSample == 32 && audioFormat == 3) {
                // 32-bit float
                file.read(reinterpret_cast<char*>(rawSamples.data()), chunkSize);
            } else {
                return false;  // Unsupported format
            }
            break;
        } else {
            // Skip unknown chunk
            file.seekg(chunkSize, std::ios::cur);
        }
    }

    if (rawSamples.empty() || numChannels == 0) {
        return false;
    }

    // Convert to stereo interleaved at target sample rate
    uint32_t inputFrames = static_cast<uint32_t>(rawSamples.size()) / numChannels;

    // Simple sample rate conversion (linear interpolation)
    double ratio = static_cast<double>(m_sampleRate) / static_cast<double>(fileSampleRate);
    uint32_t outputFrames = static_cast<uint32_t>(static_cast<double>(inputFrames) * ratio);

    region.samples.resize(outputFrames * 2);
    region.sampleFrames = outputFrames;
    region.sampleRate = m_sampleRate;

    for (uint32_t i = 0; i < outputFrames; ++i) {
        double srcPos = static_cast<double>(i) / ratio;
        uint32_t srcIdx = static_cast<uint32_t>(srcPos);
        float frac = static_cast<float>(srcPos - static_cast<double>(srcIdx));

        if (srcIdx >= inputFrames - 1) {
            srcIdx = inputFrames - 2;
            frac = 1.0f;
        }

        for (int ch = 0; ch < 2; ++ch) {
            int srcCh = (numChannels == 1) ? 0 : ch;
            float s0 = rawSamples[srcIdx * numChannels + srcCh];
            float s1 = rawSamples[(srcIdx + 1) * numChannels + srcCh];
            region.samples[i * 2 + ch] = s0 + frac * (s1 - s0);
        }
    }

    // Update loop points for sample rate conversion
    if (region.loopEnd == 0) {
        region.loopEnd = region.sampleFrames;
    } else {
        region.loopStart = static_cast<uint64_t>(static_cast<double>(region.loopStart) * ratio);
        region.loopEnd = static_cast<uint64_t>(static_cast<double>(region.loopEnd) * ratio);
    }

    region.loaded = true;
    return true;
}

bool MultiSampler::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    // Dark wood-brown background
    viz.drawBackground(bounds, VIZ_COL32(45, 35, 30, 255));

    // Get active group info
    std::string groupName = "No Preset";
    int regionCount_ = 0;
    int loNote = 127, hiNote = 0;

    if (!m_groups.empty() && m_activeGroup >= 0 && m_activeGroup < static_cast<int>(m_groups.size())) {
        const auto& group = m_groups[m_activeGroup];
        groupName = group.name;
        regionCount_ = static_cast<int>(group.regions.size());
        for (const auto& region : group.regions) {
            loNote = std::min(loNote, region.loNote);
            hiNote = std::max(hiNote, region.hiNote);
        }
    }

    // Collect active notes and available notes
    std::vector<int> activeNotes, availableNotes;
    int maxVoices_ = static_cast<int>(maxVoices);
    for (int i = 0; i < maxVoices_ && i < static_cast<int>(m_voices.size()); i++) {
        if (m_voices[i].isActive()) {
            activeNotes.push_back(m_voices[i].midiNote);
        }
    }

    if (!m_groups.empty() && m_activeGroup >= 0) {
        for (const auto& region : m_groups[m_activeGroup].regions) {
            for (int n = region.loNote; n <= region.hiNote; n++) {
                if (std::find(availableNotes.begin(), availableNotes.end(), n) == availableNotes.end()) {
                    availableNotes.push_back(n);
                }
            }
        }
    }

    // Draw mini keyboard
    int keyboardLo = std::max(36, loNote - 6);
    int keyboardHi = std::min(96, hiNote + 6);
    VizBounds keyboardBounds = bounds.sub(10, bounds.h * 0.35f, bounds.w - 20, bounds.h * 0.5f);
    viz.drawKeyboard(keyboardBounds, keyboardLo, keyboardHi, activeNotes, availableNotes);

    // Voice count dots (top left)
    for (int i = 0; i < std::min(8, maxVoices_); i++) {
        float dotX = minX + 8 + i * 8.0f;
        float intensity = (i < static_cast<int>(activeNotes.size())) ? 1.0f : 0.2f;
        viz.drawActivityDot(dotX, minY + 8, intensity, VizColors::Highlight);
    }

    // Region count (top right)
    if (regionCount_ > 0) {
        char countText[32];
        snprintf(countText, sizeof(countText), "%d smp", regionCount_);
        dl->AddText({maxX - 40, minY + 5}, VizColors::TextSecondary, countText);
    }

    // Group name (bottom)
    if (!groupName.empty() && groupName != "No Preset") {
        dl->AddText({minX + 10, maxY - 15}, VizColors::TextSecondary, groupName.c_str());
    }

    return true;
}

} // namespace vivid::audio
