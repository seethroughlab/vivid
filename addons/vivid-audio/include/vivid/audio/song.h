#pragma once

/**
 * @file song.h
 * @brief Song structure operator for section-based composition
 *
 * Defines song sections (intro, verse, chorus, etc.) that sync to a Clock,
 * enabling coordinated audio-visual changes based on musical structure.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

namespace vivid::audio {

// Forward declaration
class Clock;

/**
 * @brief A section within a song
 */
struct Section {
    std::string name;       ///< Section name (e.g., "intro", "chorus")
    uint32_t startBar = 0;  ///< Starting bar (0-indexed)
    uint32_t endBar = 0;    ///< Ending bar (exclusive)
    int repeatCount = 1;    ///< Number of times to play (0 = skip, -1 = loop forever)
};

/**
 * @brief Song structure operator
 *
 * Organizes a composition into named sections that sync to a Clock operator.
 * Useful for:
 * - Coordinating audio and visual changes at section boundaries
 * - Building arrangements with intro, verse, chorus, bridge, outro
 * - Live performance with section-based cues
 *
 * @par Example
 * @code
 * auto& song = chain.add<Song>("song");
 * song.syncTo("clock");
 *
 * // Define sections (bar numbers)
 * song.addSection("intro", 0, 8);
 * song.addSection("verse1", 8, 24);
 * song.addSection("chorus", 24, 32);
 * song.addSection("verse2", 32, 48);
 * song.addSection("chorus", 48, 56);
 * song.addSection("outro", 56, 64);
 *
 * // In update():
 * auto& song = chain.get<Song>("song");
 *
 * if (song.section() == "chorus") {
 *     particles.emitRate = 500;
 *     bloom.intensity = 2.0f;
 * } else {
 *     particles.emitRate = 50;
 *     bloom.intensity = 0.5f;
 * }
 *
 * // Smooth transitions based on section progress
 * float t = song.sectionProgress();  // 0-1 through current section
 * filter.cutoff = 500 + t * 3500;
 * @endcode
 */
class Song : public Operator {
public:
    Song() = default;
    ~Song() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Sync to a Clock operator by name
     * @param clockName Name of the Clock operator
     */
    void syncTo(const std::string& clockName) { m_clockName = clockName; }

    /**
     * @brief Add a section to the song
     * @param name Section name
     * @param startBar Starting bar (0-indexed)
     * @param endBar Ending bar (exclusive)
     * @param repeatCount Times to repeat (default 1, -1 for infinite loop)
     */
    void addSection(const std::string& name, uint32_t startBar, uint32_t endBar,
                    int repeatCount = 1);

    /**
     * @brief Clear all sections
     */
    void clearSections() { m_sections.clear(); }

    /**
     * @brief Set callback for section changes
     * @param callback Function called with (previousSection, newSection)
     */
    void onSectionChange(std::function<void(const std::string&, const std::string&)> callback) {
        m_onSectionChange = callback;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Jump to a specific section by name
     * @param name Section name
     * @return true if section found and jumped to
     */
    bool jumpToSection(const std::string& name);

    /**
     * @brief Jump to a specific bar
     * @param bar Bar number (0-indexed)
     */
    void jumpToBar(uint32_t bar);

    /**
     * @brief Jump to next section
     */
    void nextSection();

    /**
     * @brief Jump to previous section
     */
    void previousSection();

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    /**
     * @brief Get current section name
     * @return Section name, or empty string if no section
     */
    const std::string& section() const { return m_currentSection; }

    /**
     * @brief Get current section index
     * @return Index into sections list, or -1 if no section
     */
    int sectionIndex() const { return m_currentSectionIndex; }

    /**
     * @brief Get progress through current section (0-1)
     */
    float sectionProgress() const { return m_sectionProgress; }

    /**
     * @brief Get progress through entire song (0-1)
     */
    float songProgress() const { return m_songProgress; }

    /**
     * @brief Get current bar number
     */
    uint32_t currentBar() const { return m_currentBar; }

    /**
     * @brief Get current beat within bar
     */
    float currentBeat() const { return m_currentBeat; }

    /**
     * @brief Check if at start of a new section this frame
     */
    bool sectionJustStarted() const { return m_sectionJustStarted; }

    /**
     * @brief Check if at start of a new bar this frame
     */
    bool barJustStarted() const { return m_barJustStarted; }

    /**
     * @brief Get total song length in bars
     */
    uint32_t totalBars() const;

    /**
     * @brief Get number of sections
     */
    size_t sectionCount() const { return m_sections.size(); }

    /**
     * @brief Get section by index
     */
    const Section* getSection(size_t index) const;

    /**
     * @brief Get section by name
     */
    const Section* getSection(const std::string& name) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Song"; }

    /// @}

private:
    void updateFromClock();
    int findSectionAtBar(uint32_t bar) const;

    // Clock connection
    std::string m_clockName;
    Clock* m_clock = nullptr;

    // Sections
    std::vector<Section> m_sections;
    std::unordered_map<std::string, size_t> m_sectionNameIndex;

    // Current state
    std::string m_currentSection;
    int m_currentSectionIndex = -1;
    float m_sectionProgress = 0.0f;
    float m_songProgress = 0.0f;
    uint32_t m_currentBar = 0;
    float m_currentBeat = 0.0f;

    // Edge detection
    bool m_sectionJustStarted = false;
    bool m_barJustStarted = false;
    uint32_t m_lastBar = UINT32_MAX;
    int m_lastSectionIndex = -1;

    // Callbacks
    std::function<void(const std::string&, const std::string&)> m_onSectionChange;
};

} // namespace vivid::audio
