#pragma once

/**
 * @file file_buffer.h
 * @brief Per-file state for the tabbed editor
 *
 * FileBuffer holds all state for a single open file:
 * - File path and content (lines)
 * - Cursor and scroll position
 * - Selection state
 * - Dirty flag
 *
 * These are preserved when switching tabs.
 */

#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Per-file state for editor tabs
 */
class FileBuffer {
public:
    FileBuffer() = default;
    explicit FileBuffer(const std::string& path);

    /**
     * @brief Load file from disk
     * @param path File path to load
     * @return true on success
     */
    bool load(const std::string& path);

    /**
     * @brief Reload file from disk (discard changes)
     * @return true on success
     */
    bool reload();

    /**
     * @brief Save file to disk
     * @return true on success
     */
    bool save();

    /**
     * @brief Get the file path
     */
    const std::string& path() const { return m_filePath; }

    /**
     * @brief Get just the filename (e.g., "chain.cpp")
     */
    std::string filename() const;

    /**
     * @brief Check if file has unsaved changes
     */
    bool isDirty() const { return m_dirty; }

    /**
     * @brief Mark the file as dirty (has unsaved changes)
     */
    void setDirty(bool dirty) { m_dirty = dirty; }

    /**
     * @brief Check if file is a WGSL shader
     */
    bool isWgsl() const;

    // -------------------------------------------------------------------------
    /// @name Content access
    /// @{

    /**
     * @brief Get all lines
     */
    std::vector<std::string>& lines() { return m_lines; }
    const std::vector<std::string>& lines() const { return m_lines; }

    /**
     * @brief Ensure a line exists (extends buffer if needed)
     */
    void ensureLine(int line);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Cursor state (preserved across tab switches)
    /// @{

    int cursorLine = 0;
    int cursorCol = 0;
    int scrollOffset = 0;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Selection state (preserved across tab switches)
    /// @{

    bool hasSelection = false;
    int selStartLine = 0;
    int selStartCol = 0;
    int selEndLine = 0;
    int selEndCol = 0;

    /**
     * @brief Clear selection
     */
    void clearSelection();

    /**
     * @brief Start selection at current cursor
     */
    void startSelection();

    /**
     * @brief Update selection end to current cursor
     */
    void updateSelection();

    /**
     * @brief Get normalized selection (start <= end)
     */
    void getNormalizedSelection(int& startLine, int& startCol, int& endLine, int& endCol) const;

    /**
     * @brief Get selected text
     */
    std::string getSelectedText() const;

    /**
     * @brief Delete selected text
     */
    void deleteSelection();

    /**
     * @brief Select all text
     */
    void selectAll();

    /**
     * @brief Clamp cursor to valid position
     */
    void clampCursor();

    /**
     * @brief Scroll to keep cursor visible
     */
    void scroll(int delta);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Error state
    /// @{

    int errorLine = 0;
    std::string errorMessage;

    /// @}

private:
    std::string m_filePath;
    std::vector<std::string> m_lines;
    bool m_dirty = false;
};

} // namespace vivid
