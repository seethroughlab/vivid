// FileBuffer implementation
// Per-file state for the tabbed editor

#include <vivid/devtools/file_buffer.h>
#include <fstream>
#include <algorithm>

namespace vivid {

FileBuffer::FileBuffer(const std::string& path) {
    load(path);
}

bool FileBuffer::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    m_filePath = path;
    m_lines.clear();
    m_dirty = false;
    cursorLine = 0;
    cursorCol = 0;
    scrollOffset = 0;
    clearSelection();
    errorLine = 0;
    errorMessage.clear();

    std::string line;
    while (std::getline(file, line)) {
        m_lines.push_back(line);
    }

    if (m_lines.empty()) {
        m_lines.push_back("");
    }

    return true;
}

bool FileBuffer::reload() {
    if (m_filePath.empty()) {
        return false;
    }
    return load(m_filePath);
}

bool FileBuffer::save() {
    if (m_filePath.empty()) {
        return false;
    }

    std::ofstream file(m_filePath);
    if (!file.is_open()) {
        return false;
    }

    for (size_t i = 0; i < m_lines.size(); i++) {
        file << m_lines[i];
        if (i < m_lines.size() - 1) {
            file << '\n';
        }
    }

    m_dirty = false;
    return true;
}

std::string FileBuffer::filename() const {
    if (m_filePath.empty()) {
        return "untitled";
    }

    size_t pos = m_filePath.find_last_of("/\\");
    if (pos != std::string::npos) {
        return m_filePath.substr(pos + 1);
    }
    return m_filePath;
}

bool FileBuffer::isWgsl() const {
    return m_filePath.find(".wgsl") != std::string::npos;
}

void FileBuffer::ensureLine(int line) {
    while (m_lines.size() <= static_cast<size_t>(line)) {
        m_lines.push_back("");
    }
}

void FileBuffer::clearSelection() {
    hasSelection = false;
}

void FileBuffer::startSelection() {
    selStartLine = cursorLine;
    selStartCol = cursorCol;
    selEndLine = cursorLine;
    selEndCol = cursorCol;
    hasSelection = true;
}

void FileBuffer::updateSelection() {
    selEndLine = cursorLine;
    selEndCol = cursorCol;
}

void FileBuffer::getNormalizedSelection(int& startLine, int& startCol, int& endLine, int& endCol) const {
    if (selStartLine < selEndLine || (selStartLine == selEndLine && selStartCol <= selEndCol)) {
        startLine = selStartLine;
        startCol = selStartCol;
        endLine = selEndLine;
        endCol = selEndCol;
    } else {
        startLine = selEndLine;
        startCol = selEndCol;
        endLine = selStartLine;
        endCol = selStartCol;
    }
}

std::string FileBuffer::getSelectedText() const {
    if (!hasSelection) return "";

    int startLine, startCol, endLine, endCol;
    getNormalizedSelection(startLine, startCol, endLine, endCol);

    if (startLine == endLine) {
        return m_lines[startLine].substr(startCol, endCol - startCol);
    }

    std::string result;
    result += m_lines[startLine].substr(startCol);
    result += '\n';
    for (int i = startLine + 1; i < endLine; i++) {
        result += m_lines[i];
        result += '\n';
    }
    result += m_lines[endLine].substr(0, endCol);
    return result;
}

void FileBuffer::deleteSelection() {
    if (!hasSelection) return;

    int startLine, startCol, endLine, endCol;
    getNormalizedSelection(startLine, startCol, endLine, endCol);

    if (startLine == endLine) {
        m_lines[startLine].erase(startCol, endCol - startCol);
    } else {
        std::string newLine = m_lines[startLine].substr(0, startCol) + m_lines[endLine].substr(endCol);
        m_lines[startLine] = newLine;
        m_lines.erase(m_lines.begin() + startLine + 1, m_lines.begin() + endLine + 1);
    }

    cursorLine = startLine;
    cursorCol = startCol;
    hasSelection = false;
    m_dirty = true;
}

void FileBuffer::selectAll() {
    if (m_lines.empty()) return;
    selStartLine = 0;
    selStartCol = 0;
    selEndLine = static_cast<int>(m_lines.size()) - 1;
    selEndCol = static_cast<int>(m_lines[selEndLine].size());
    hasSelection = true;
    cursorLine = selEndLine;
    cursorCol = selEndCol;
}

void FileBuffer::clampCursor() {
    cursorLine = std::max(0, std::min(cursorLine, static_cast<int>(m_lines.size()) - 1));
    if (cursorLine >= 0 && cursorLine < static_cast<int>(m_lines.size())) {
        cursorCol = std::max(0, std::min(cursorCol, static_cast<int>(m_lines[cursorLine].size())));
    } else {
        cursorCol = 0;
    }
}

void FileBuffer::scroll(int delta) {
    scrollOffset = std::max(0, scrollOffset + delta);
    int maxScroll = std::max(0, static_cast<int>(m_lines.size()) - 10);
    scrollOffset = std::min(scrollOffset, maxScroll);
}

} // namespace vivid
