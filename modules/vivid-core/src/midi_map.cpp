// MidiMapStore — MIDI CC-to-parameter mapping with learn mode and persistence

#include <vivid/midi_map.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

namespace vivid {

// ---------------------------------------------------------------------------
// Mapping CRUD
// ---------------------------------------------------------------------------

void MidiMapStore::add(const MidiMapping& m) {
    // Remove any existing mapping for same target param
    remove(m.targetOp, m.paramName);
    m_mappings.push_back(m);
    m_version++;
}

void MidiMapStore::remove(const std::string& targetOp, const std::string& paramName) {
    auto it = std::remove_if(m_mappings.begin(), m_mappings.end(),
        [&](const MidiMapping& m) {
            return m.targetOp == targetOp && m.paramName == paramName;
        });
    if (it != m_mappings.end()) {
        m_mappings.erase(it, m_mappings.end());
        m_version++;
    }
}

void MidiMapStore::removeByCC(uint8_t cc, const std::string& midiInOp) {
    auto it = std::remove_if(m_mappings.begin(), m_mappings.end(),
        [&](const MidiMapping& m) {
            if (m.cc != cc) return false;
            if (!midiInOp.empty() && !m.midiInOp.empty() && m.midiInOp != midiInOp) return false;
            return true;
        });
    if (it != m_mappings.end()) {
        m_mappings.erase(it, m_mappings.end());
        m_version++;
    }
}

void MidiMapStore::clear() {
    if (!m_mappings.empty()) {
        m_mappings.clear();
        m_version++;
    }
}

const MidiMapping* MidiMapStore::find(const std::string& targetOp,
                                       const std::string& paramName) const {
    for (const auto& m : m_mappings) {
        if (m.targetOp == targetOp && m.paramName == paramName) {
            return &m;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Learn State Machine
// ---------------------------------------------------------------------------

void MidiMapStore::startLearn(const std::string& targetOp, const std::string& paramName,
                               float minVal, float maxVal) {
    m_learning = true;
    m_learnTargetOp = targetOp;
    m_learnParamName = paramName;
    m_learnMinVal = minVal;
    m_learnMaxVal = maxVal;
}

void MidiMapStore::cancelLearn() {
    m_learning = false;
    m_learnTargetOp.clear();
    m_learnParamName.clear();
}

bool MidiMapStore::completeLearn(uint8_t cc, const std::string& midiInOp) {
    if (!m_learning) return false;

    // Remove any existing mapping for same CC+midiInOp (overwrite)
    removeByCC(cc, midiInOp);

    // Remove any existing mapping for same target param (one CC per param)
    remove(m_learnTargetOp, m_learnParamName);

    // Add new mapping
    MidiMapping mapping;
    mapping.midiInOp = midiInOp;
    mapping.cc = cc;
    mapping.targetOp = m_learnTargetOp;
    mapping.paramName = m_learnParamName;
    mapping.minVal = m_learnMinVal;
    mapping.maxVal = m_learnMaxVal;
    m_mappings.push_back(mapping);

    // End learn mode
    m_learning = false;
    m_learnTargetOp.clear();
    m_learnParamName.clear();
    m_version++;

    return true;
}

// ---------------------------------------------------------------------------
// Persistence (JSON)
// ---------------------------------------------------------------------------

bool MidiMapStore::save(const std::string& path) const {
    try {
        json j;
        j["mappings"] = json::array();

        for (const auto& m : m_mappings) {
            json jm;
            jm["midiIn"] = m.midiInOp;
            jm["cc"] = m.cc;
            jm["operator"] = m.targetOp;
            jm["parameter"] = m.paramName;
            jm["min"] = m.minVal;
            jm["max"] = m.maxVal;
            j["mappings"].push_back(jm);
        }

        std::ofstream file(path);
        if (!file.is_open()) return false;
        file << j.dump(2);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[MidiMap] Failed to save: " << e.what() << std::endl;
        return false;
    }
}

bool MidiMapStore::load(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        json j = json::parse(file);

        m_mappings.clear();
        m_learning = false;

        if (!j.contains("mappings") || !j["mappings"].is_array()) return false;

        for (const auto& jm : j["mappings"]) {
            MidiMapping m;
            m.midiInOp = jm.value("midiIn", "");
            m.cc = jm.value("cc", 0);
            m.targetOp = jm.value("operator", "");
            m.paramName = jm.value("parameter", "");
            m.minVal = jm.value("min", 0.0f);
            m.maxVal = jm.value("max", 1.0f);
            m_mappings.push_back(m);
        }

        m_version++;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[MidiMap] Failed to load: " << e.what() << std::endl;
        return false;
    }
}

} // namespace vivid
