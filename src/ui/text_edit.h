#pragma once

#include <string>
#include <algorithm>
#include <functional>

namespace vivid::ui {

struct TextEditState {
    int cursor = 0;       // insertion point (0 = before first char)
    int sel_start = -1;   // -1 = no selection; otherwise anchor point

    bool has_selection() const { return sel_start >= 0 && sel_start != cursor; }

    int sel_min() const { return std::min(sel_start, cursor); }
    int sel_max() const { return std::max(sel_start, cursor); }

    void reset(int end_pos) {
        cursor = end_pos;
        sel_start = -1;
    }

    void select_all(int len) {
        sel_start = 0;
        cursor = len;
    }

    // Clamp cursor/selection to valid range for buffer of given length
    void clamp(int len) {
        cursor = std::max(0, std::min(cursor, len));
        if (sel_start >= 0) sel_start = std::max(0, std::min(sel_start, len));
    }
};

// --- Free functions operating on std::string& buf + TextEditState& st ---

using CharFilter = std::function<bool(char)>;

// Insert text at cursor position, replacing selection if any.
// Characters are filtered through pred; insertion stops at max_len.
inline void text_edit_insert(std::string& buf, TextEditState& st,
                             const std::string& text, CharFilter pred = nullptr,
                             size_t max_len = SIZE_MAX) {
    // Delete selection first
    if (st.has_selection()) {
        int lo = st.sel_min();
        int hi = st.sel_max();
        buf.erase(lo, hi - lo);
        st.cursor = lo;
        st.sel_start = -1;
    }

    // Insert filtered characters
    for (char ch : text) {
        if (pred && !pred(ch)) continue;
        if (buf.size() >= max_len) break;
        buf.insert(buf.begin() + st.cursor, ch);
        st.cursor++;
    }
}

// Backspace: delete selection, or single char before cursor
inline void text_edit_backspace(std::string& buf, TextEditState& st) {
    if (st.has_selection()) {
        int lo = st.sel_min();
        int hi = st.sel_max();
        buf.erase(lo, hi - lo);
        st.cursor = lo;
        st.sel_start = -1;
    } else if (st.cursor > 0) {
        buf.erase(st.cursor - 1, 1);
        st.cursor--;
    }
}

// Forward delete: delete selection, or single char at cursor
inline void text_edit_delete_forward(std::string& buf, TextEditState& st) {
    if (st.has_selection()) {
        int lo = st.sel_min();
        int hi = st.sel_max();
        buf.erase(lo, hi - lo);
        st.cursor = lo;
        st.sel_start = -1;
    } else if (st.cursor < static_cast<int>(buf.size())) {
        buf.erase(st.cursor, 1);
    }
}

// Move cursor left; shift extends selection
inline void text_edit_move_left(TextEditState& st, bool shift) {
    if (shift) {
        if (st.sel_start < 0) st.sel_start = st.cursor;
        if (st.cursor > 0) st.cursor--;
    } else {
        if (st.has_selection()) {
            st.cursor = st.sel_min();
        } else if (st.cursor > 0) {
            st.cursor--;
        }
        st.sel_start = -1;
    }
}

// Move cursor right; shift extends selection
inline void text_edit_move_right(TextEditState& st, int len, bool shift) {
    if (shift) {
        if (st.sel_start < 0) st.sel_start = st.cursor;
        if (st.cursor < len) st.cursor++;
    } else {
        if (st.has_selection()) {
            st.cursor = st.sel_max();
        } else if (st.cursor < len) {
            st.cursor++;
        }
        st.sel_start = -1;
    }
}

// Home / Cmd+Left: move to start
inline void text_edit_home(TextEditState& st, bool shift) {
    if (shift) {
        if (st.sel_start < 0) st.sel_start = st.cursor;
    } else {
        st.sel_start = -1;
    }
    st.cursor = 0;
}

// End / Cmd+Right: move to end
inline void text_edit_end(TextEditState& st, int len, bool shift) {
    if (shift) {
        if (st.sel_start < 0) st.sel_start = st.cursor;
    } else {
        st.sel_start = -1;
    }
    st.cursor = len;
}

// Move cursor up one logical line, preserving column. Shift extends selection.
inline void text_edit_move_up(const std::string& buf, TextEditState& st, bool shift) {
    int cur = st.cursor;
    int cur_line_start = 0;
    for (int i = cur - 1; i >= 0; --i) {
        if (buf[i] == '\n') { cur_line_start = i + 1; break; }
    }
    int col = cur - cur_line_start;
    if (shift) {
        if (st.sel_start < 0) st.sel_start = st.cursor;
    } else {
        st.sel_start = -1;
    }
    if (cur_line_start == 0) {
        st.cursor = 0;
        return;
    }
    int prev_line_end = cur_line_start - 1;
    int prev_line_start = 0;
    for (int i = prev_line_end - 1; i >= 0; --i) {
        if (buf[i] == '\n') { prev_line_start = i + 1; break; }
    }
    int prev_line_len = prev_line_end - prev_line_start;
    st.cursor = prev_line_start + std::min(col, prev_line_len);
}

// Move cursor down one logical line, preserving column. Shift extends selection.
inline void text_edit_move_down(const std::string& buf, TextEditState& st, bool shift) {
    int cur = st.cursor;
    int len = static_cast<int>(buf.size());
    int cur_line_start = 0;
    for (int i = cur - 1; i >= 0; --i) {
        if (buf[i] == '\n') { cur_line_start = i + 1; break; }
    }
    int col = cur - cur_line_start;
    int cur_line_end = len;
    for (int i = cur; i < len; ++i) {
        if (buf[i] == '\n') { cur_line_end = i; break; }
    }
    if (shift) {
        if (st.sel_start < 0) st.sel_start = st.cursor;
    } else {
        st.sel_start = -1;
    }
    if (cur_line_end == len) {
        st.cursor = len;
        return;
    }
    int next_line_start = cur_line_end + 1;
    int next_line_end = len;
    for (int i = next_line_start; i < len; ++i) {
        if (buf[i] == '\n') { next_line_end = i; break; }
    }
    int next_line_len = next_line_end - next_line_start;
    st.cursor = next_line_start + std::min(col, next_line_len);
}

// Select all
inline void text_edit_select_all(TextEditState& st, int len) {
    st.sel_start = 0;
    st.cursor = len;
}

// Copy selected text (returns empty string if no selection)
inline std::string text_edit_copy(const std::string& buf, const TextEditState& st) {
    if (!st.has_selection()) return {};
    int lo = st.sel_min();
    int hi = st.sel_max();
    return buf.substr(lo, hi - lo);
}

// Cut selected text (returns cut string, modifies buffer)
inline std::string text_edit_cut(std::string& buf, TextEditState& st) {
    if (!st.has_selection()) return {};
    int lo = st.sel_min();
    int hi = st.sel_max();
    std::string result = buf.substr(lo, hi - lo);
    buf.erase(lo, hi - lo);
    st.cursor = lo;
    st.sel_start = -1;
    return result;
}

} // namespace vivid::ui
