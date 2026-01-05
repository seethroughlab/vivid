/**
 * @file song.cpp
 * @brief Implementation of song structure operator
 */

#include <vivid/audio/song.h>
#include <vivid/audio/clock.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <algorithm>

namespace vivid::audio {

void Song::addSection(const std::string& name, uint32_t startBar, uint32_t endBar,
                      int repeatCount) {
    Section section;
    section.name = name;
    section.startBar = startBar;
    section.endBar = endBar;
    section.repeatCount = repeatCount;

    m_sections.push_back(section);
    m_sectionNameIndex[name] = m_sections.size() - 1;
}

bool Song::jumpToSection(const std::string& name) {
    auto it = m_sectionNameIndex.find(name);
    if (it == m_sectionNameIndex.end()) {
        return false;
    }

    size_t idx = it->second;
    if (idx >= m_sections.size()) {
        return false;
    }

    // Jump to start of section
    jumpToBar(m_sections[idx].startBar);
    return true;
}

void Song::jumpToBar(uint32_t bar) {
    m_currentBar = bar;
    m_currentBeat = 0.0f;

    // TODO: Sync clock to this bar position
    // This would require Clock to support seeking
}

void Song::nextSection() {
    if (m_currentSectionIndex < 0 || m_sections.empty()) return;

    int nextIdx = m_currentSectionIndex + 1;
    if (nextIdx >= static_cast<int>(m_sections.size())) {
        nextIdx = 0;  // Wrap to beginning
    }

    jumpToBar(m_sections[nextIdx].startBar);
}

void Song::previousSection() {
    if (m_currentSectionIndex < 0 || m_sections.empty()) return;

    int prevIdx = m_currentSectionIndex - 1;
    if (prevIdx < 0) {
        prevIdx = static_cast<int>(m_sections.size()) - 1;  // Wrap to end
    }

    jumpToBar(m_sections[prevIdx].startBar);
}

uint32_t Song::totalBars() const {
    uint32_t maxBar = 0;
    for (const auto& section : m_sections) {
        maxBar = std::max(maxBar, section.endBar);
    }
    return maxBar;
}

const Section* Song::getSection(size_t index) const {
    if (index >= m_sections.size()) return nullptr;
    return &m_sections[index];
}

const Section* Song::getSection(const std::string& name) const {
    auto it = m_sectionNameIndex.find(name);
    if (it == m_sectionNameIndex.end()) return nullptr;
    if (it->second >= m_sections.size()) return nullptr;
    return &m_sections[it->second];
}

void Song::init(Context& ctx) {
    // Find clock operator
    if (!m_clockName.empty()) {
        Operator* op = ctx.chain().getByName(m_clockName);
        m_clock = dynamic_cast<Clock*>(op);
    }
}

void Song::process(Context& ctx) {
    // Reset edge flags
    m_sectionJustStarted = false;
    m_barJustStarted = false;

    // Update from clock
    if (m_clock) {
        updateFromClock();
    }

    // Detect bar change
    if (m_currentBar != m_lastBar) {
        m_barJustStarted = true;
        m_lastBar = m_currentBar;
    }

    // Detect section change
    if (m_currentSectionIndex != m_lastSectionIndex) {
        m_sectionJustStarted = true;

        // Fire callback
        if (m_onSectionChange) {
            std::string prevSection;
            if (m_lastSectionIndex >= 0 && m_lastSectionIndex < static_cast<int>(m_sections.size())) {
                prevSection = m_sections[m_lastSectionIndex].name;
            }
            m_onSectionChange(prevSection, m_currentSection);
        }

        m_lastSectionIndex = m_currentSectionIndex;
    }
}

void Song::cleanup() {
    m_clock = nullptr;
}

void Song::updateFromClock() {
    if (!m_clock) return;

    // Get current position from clock
    m_currentBar = m_clock->bar();
    m_currentBeat = m_clock->beat();

    // Find which section we're in
    m_currentSectionIndex = findSectionAtBar(m_currentBar);

    if (m_currentSectionIndex >= 0 && m_currentSectionIndex < static_cast<int>(m_sections.size())) {
        const Section& section = m_sections[m_currentSectionIndex];
        m_currentSection = section.name;

        // Calculate progress through section
        uint32_t sectionLength = section.endBar - section.startBar;
        if (sectionLength > 0) {
            float barInSection = static_cast<float>(m_currentBar - section.startBar) + m_currentBeat / 4.0f;
            m_sectionProgress = barInSection / static_cast<float>(sectionLength);
            m_sectionProgress = std::max(0.0f, std::min(1.0f, m_sectionProgress));
        } else {
            m_sectionProgress = 0.0f;
        }
    } else {
        m_currentSection.clear();
        m_sectionProgress = 0.0f;
    }

    // Calculate overall song progress
    uint32_t total = totalBars();
    if (total > 0) {
        float barPos = static_cast<float>(m_currentBar) + m_currentBeat / 4.0f;
        m_songProgress = barPos / static_cast<float>(total);
        m_songProgress = std::max(0.0f, std::min(1.0f, m_songProgress));
    } else {
        m_songProgress = 0.0f;
    }
}

int Song::findSectionAtBar(uint32_t bar) const {
    // Find the section that contains this bar
    // If multiple sections overlap, return the first one
    for (size_t i = 0; i < m_sections.size(); ++i) {
        const Section& section = m_sections[i];
        if (bar >= section.startBar && bar < section.endBar) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace vivid::audio
