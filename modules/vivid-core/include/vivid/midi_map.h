#pragma once

/**
 * @file midi_map.h
 * @brief MIDI CC-to-parameter mapping with learn mode and persistence
 *
 * MidiMapStore manages MIDI CC mappings with:
 * - CRUD operations for mappings
 * - Learn state machine (click param, move knob, done)
 * - JSON persistence to vivid-midi-map.json
 *
 * Lives in vivid-core so both vivid-midi (MidiIn) and vivid-devtools
 * (InspectorPanel) can access it through Chain::midiMappings().
 */

#include <string>
#include <vector>
#include <cstdint>

namespace vivid {

/**
 * @brief A single MIDI CC-to-parameter mapping
 */
struct MidiMapping {
    std::string midiInOp;     ///< MidiIn operator name ("" = any/first)
    uint8_t cc = 0;           ///< MIDI CC number (0-127)
    std::string targetOp;     ///< Target operator name
    std::string paramName;    ///< Target parameter name
    float minVal = 0.0f;      ///< Output range min
    float maxVal = 1.0f;      ///< Output range max
};

/**
 * @brief Manages MIDI CC mappings with learn mode and persistence
 *
 * Owned by Chain (like SnapshotStore). MidiIn reads from it to sync
 * its internal ccMappings. InspectorPanel drives the learn UI.
 *
 * @par Example
 * @code
 * auto& store = chain.midiMappings();
 * store.startLearn("synth", "filterCutoff", 200.0f, 8000.0f);
 * // ... MidiIn detects CC and calls completeLearn() ...
 * store.save("project/vivid-midi-map.json");
 * @endcode
 */
class MidiMapStore {
public:
    MidiMapStore() = default;

    // -------------------------------------------------------------------------
    /// @name Mapping CRUD
    /// @{

    /** @brief Add a mapping (replaces any existing for same targetOp+paramName) */
    void add(const MidiMapping& m);

    /** @brief Remove mapping for a specific parameter */
    void remove(const std::string& targetOp, const std::string& paramName);

    /** @brief Remove all mappings for a specific CC (optionally scoped to a MidiIn) */
    void removeByCC(uint8_t cc, const std::string& midiInOp = "");

    /** @brief Remove all mappings */
    void clear();

    /** @brief Get all mappings */
    [[nodiscard]] const std::vector<MidiMapping>& list() const { return m_mappings; }

    /** @brief Get mapping count */
    [[nodiscard]] int size() const { return static_cast<int>(m_mappings.size()); }

    /** @brief Find mapping for a specific parameter (nullptr if none) */
    [[nodiscard]] const MidiMapping* find(const std::string& targetOp,
                                           const std::string& paramName) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Persistence
    /// @{

    /** @brief Save mappings to JSON file */
    bool save(const std::string& path) const;

    /** @brief Load mappings from JSON file */
    bool load(const std::string& path);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Learn State Machine
    /// @{

    /** @brief Start learning: next CC received will map to this parameter */
    void startLearn(const std::string& targetOp, const std::string& paramName,
                    float minVal, float maxVal);

    /** @brief Cancel learn mode */
    void cancelLearn();

    /** @brief Check if currently in learn mode */
    [[nodiscard]] bool isLearning() const { return m_learning; }

    /** @brief Get the target operator name during learn */
    [[nodiscard]] const std::string& learnTargetOp() const { return m_learnTargetOp; }

    /** @brief Get the target parameter name during learn */
    [[nodiscard]] const std::string& learnParamName() const { return m_learnParamName; }

    /**
     * @brief Complete learn with a CC number (called by MidiIn)
     * @param cc MIDI CC number that was received
     * @param midiInOp Name of the MidiIn operator that received it
     * @return true if learn was consumed
     */
    bool completeLearn(uint8_t cc, const std::string& midiInOp);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Version Tracking
    /// @{

    /** @brief Version counter (bumped on any mutation) */
    [[nodiscard]] uint32_t version() const { return m_version; }

    /// @}

private:
    std::vector<MidiMapping> m_mappings;
    uint32_t m_version = 0;

    // Learn state
    bool m_learning = false;
    std::string m_learnTargetOp;
    std::string m_learnParamName;
    float m_learnMinVal = 0.0f;
    float m_learnMaxVal = 1.0f;
};

} // namespace vivid
