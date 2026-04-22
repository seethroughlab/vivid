// Focused unit tests for vivid::ui::ui_text_field. Drives the widget
// via a synthesized VividEditorContext — typing, cursor movement,
// selection, deletion, commit/cancel, clipboard round-trip, focus and
// defocus, buffer overflow safety.
//
// The widget is header-only in editor_ui.h; this test is the only
// compilation unit that exercises it end-to-end without a live editor.

#include "operator_api/editor_ui.h"
#include "operator_api/editor_keys.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace ui = ::vivid::ui;
namespace ek = ::vivid::editor_keys;

namespace {

void noop_draw_rect(void*, float, float, float, float, VividColor) {}
void noop_draw_rounded_rect(void*, float, float, float, float, float, VividColor) {}
void noop_draw_text(void*, float, float, const char*, VividColor, float) {}
void noop_draw_line(void*, float, float, float, float, float, VividColor) {}
float fake_text_width(void*, const char* text, float scale) {
    const std::size_t len = text ? std::strlen(text) : 0;
    return static_cast<float>(len) * 7.0f * scale;
}
float fake_line_height(void*) { return 14.0f; }
void noop_push_clip(void*, float, float, float, float) {}
void noop_pop_clip(void*) {}

VividDrawAPI make_draw_api() {
    VividDrawAPI draw{};
    draw.draw_rect         = noop_draw_rect;
    draw.draw_rounded_rect = noop_draw_rounded_rect;
    draw.draw_text         = noop_draw_text;
    draw.draw_line         = noop_draw_line;
    draw.text_width        = fake_text_width;
    draw.line_height       = fake_line_height;
    draw.push_clip_rect    = noop_push_clip;
    draw.pop_clip_rect     = noop_pop_clip;
    return draw;
}

// Fake clipboard backed by a std::string captured via the host API.
struct Clipboard {
    std::string contents;
};
const char* cb_get(void* opaque) {
    return static_cast<Clipboard*>(opaque)->contents.c_str();
}
void cb_set(void* opaque, const char* s) {
    static_cast<Clipboard*>(opaque)->contents = s ? s : "";
}

struct Harness {
    std::vector<char> buffer;
    ui::TextFieldState state{};
    Clipboard clipboard{};
    std::vector<VividEditorEvent> events;
    VividEditorContext ctx{};
    const ui::Rect rect{10.0f, 10.0f, 200.0f, 24.0f};

    Harness(std::size_t capacity = 64, const char* initial = "")
        : buffer(capacity, '\0') {
        if (initial) std::strncpy(buffer.data(), initial, capacity - 1);
        ctx.surface_width  = 400.0f;
        ctx.surface_height = 80.0f;
        ctx.dpi_scale      = 1.0f;
        ctx.draw           = make_draw_api();
        ctx.mouse          = {};
        ctx.time           = 0.0;
        ctx.host.opaque             = &clipboard;
        ctx.host.get_clipboard_text = cb_get;
        ctx.host.set_clipboard_text = cb_set;
        refresh_events();
    }
    void refresh_events() {
        ctx.events      = events.empty() ? nullptr : events.data();
        ctx.event_count = static_cast<uint32_t>(events.size());
    }
    void clear_input() {
        events.clear();
        refresh_events();
        ctx.mouse = {};
    }
    ui::TextFieldResult draw() {
        refresh_events();
        ctx.wants_keyboard = 0;
        return ui::ui_text_field(ctx, rect,
                                 buffer.data(), buffer.size(),
                                 &state, "(placeholder)");
    }
    std::string text() const { return std::string(buffer.data()); }
};

VividEditorEvent char_ev(uint32_t codepoint) {
    VividEditorEvent e{};
    e.type      = VIVID_EDITOR_EVENT_CHAR;
    e.codepoint = codepoint;
    return e;
}
VividEditorEvent key_ev(int k, int mods = 0) {
    VividEditorEvent e{};
    e.type      = VIVID_EDITOR_EVENT_KEY;
    e.key       = k;
    e.action    = ek::kPress;
    e.modifiers = mods;
    return e;
}
// Click at x, y (editor-local pixels).
void click(Harness& h, float x, float y) {
    h.ctx.mouse.x = x;
    h.ctx.mouse.y = y;
    h.ctx.mouse.left_clicked = 1;
    h.ctx.mouse.left_down    = 1;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: vivid::ui::ui_text_field ===\n\n");

    // --- Click inside focuses; click outside defocuses ---
    {
        Harness h;
        click(h, 20.0f, 22.0f);  // inside rect
        auto r = h.draw();
        check(r.focused,          "click inside focuses the field");
        check(h.state.focused,    "state reflects focused");
        check(h.ctx.wants_keyboard == 1, "focus sets wants_keyboard");

        h.clear_input();
        click(h, 300.0f, 22.0f);  // outside rect
        r = h.draw();
        check(!r.focused,         "click outside defocuses");
        check(!h.state.focused,   "state reflects defocus");
    }

    // --- Typing ASCII CHAR events appends at cursor ---
    {
        Harness h;
        h.state.focused = true;  // start focused so CHAR events are accepted
        h.events = {char_ev('h'), char_ev('i')};
        auto r = h.draw();
        check(r.changed,          "typing mutates buffer");
        check(h.text() == "hi",   "buffer reads 'hi'");
        check(h.state.cursor == 2, "cursor advanced past inserted chars");
    }

    // --- Arrow keys move cursor; Home/End jump ---
    {
        Harness h(64, "abcdef");
        h.state.focused = true;
        h.state.cursor  = 3;
        h.events = {key_ev(ek::kLeft)};
        h.draw();
        check(h.state.cursor == 2, "kLeft decrements cursor");

        h.clear_input();
        h.events = {key_ev(ek::kRight), key_ev(ek::kRight)};
        h.draw();
        check(h.state.cursor == 4, "kRight advances cursor");

        h.clear_input();
        h.events = {key_ev(ek::kHome)};
        h.draw();
        check(h.state.cursor == 0, "kHome jumps to 0");

        h.clear_input();
        h.events = {key_ev(ek::kEnd)};
        h.draw();
        check(h.state.cursor == 6, "kEnd jumps to end of buffer");
    }

    // --- Shift+Right extends selection from anchor ---
    {
        Harness h(64, "hello");
        h.state.focused = true;
        h.state.cursor  = 1;
        h.events = {
            key_ev(ek::kRight, ek::kModShift),
            key_ev(ek::kRight, ek::kModShift),
        };
        h.draw();
        check(h.state.selection_anchor == 1, "anchor seeded at starting cursor");
        check(h.state.cursor == 3,           "cursor advanced with shift");
    }

    // --- Backspace removes char before cursor ---
    {
        Harness h(64, "abcd");
        h.state.focused = true;
        h.state.cursor  = 3;
        h.events = {key_ev(ek::kBackspace)};
        auto r = h.draw();
        check(r.changed,           "backspace reports changed");
        check(h.text() == "abd",   "backspace removed 'c'");
        check(h.state.cursor == 2, "cursor stepped back after backspace");
    }

    // --- Delete removes char at cursor ---
    {
        Harness h(64, "abcd");
        h.state.focused = true;
        h.state.cursor  = 1;
        h.events = {key_ev(ek::kDelete)};
        h.draw();
        check(h.text() == "acd",   "delete removed 'b'");
        check(h.state.cursor == 1, "delete leaves cursor in place");
    }

    // --- Backspace with selection removes the range ---
    {
        Harness h(64, "abcdef");
        h.state.focused = true;
        h.state.cursor  = 4;
        h.state.selection_anchor = 1;  // select "bcd"
        h.events = {key_ev(ek::kBackspace)};
        h.draw();
        check(h.text() == "aef",            "backspace removed selection 'bcd'");
        check(h.state.cursor == 1,          "cursor lands at selection start");
        check(h.state.selection_anchor < 0, "selection cleared");
    }

    // --- Typing with selection replaces the range ---
    {
        Harness h(64, "foo bar");
        h.state.focused = true;
        h.state.cursor  = 7;
        h.state.selection_anchor = 4;  // select "bar"
        h.events = {char_ev('b'), char_ev('a'), char_ev('z')};
        h.draw();
        check(h.text() == "foo baz",
              "typing over selection replaces the selected text");
    }

    // --- Enter commits, Escape cancels ---
    {
        Harness h(64, "xyz");
        h.state.focused = true;
        h.events = {key_ev(ek::kEnter)};
        auto r = h.draw();
        check(r.committed, "Enter sets committed");
        check(h.text() == "xyz", "Enter doesn't mutate buffer");

        h.clear_input();
        h.state.focused = true;
        h.events = {key_ev(ek::kEscape)};
        r = h.draw();
        check(r.cancelled,       "Escape sets cancelled");
        check(!h.state.focused,  "Escape defocuses");
        check(h.text() == "xyz", "Escape doesn't mutate buffer");
    }

    // --- Cmd+A selects all ---
    {
        Harness h(64, "hello");
        h.state.focused = true;
        h.state.cursor  = 2;
        h.events = {key_ev(ek::kA, ek::kModSuper)};
        h.draw();
        check(h.state.selection_anchor == 0, "Cmd+A anchors at 0");
        check(h.state.cursor == 5,           "Cmd+A cursor at end");
    }

    // --- Cmd+C copies selection to clipboard ---
    {
        Harness h(64, "clipboard-test");
        h.state.focused = true;
        h.state.cursor  = 9;
        h.state.selection_anchor = 0;
        h.events = {key_ev(ek::kC, ek::kModSuper)};
        h.draw();
        check(h.clipboard.contents == "clipboard",
              "Cmd+C writes selection to clipboard");
        check(h.text() == "clipboard-test",
              "Cmd+C doesn't mutate buffer");
    }

    // --- Cmd+X cuts selection ---
    {
        Harness h(64, "foo bar");
        h.state.focused = true;
        h.state.cursor  = 7;
        h.state.selection_anchor = 4;
        h.events = {key_ev(ek::kX, ek::kModSuper)};
        h.draw();
        check(h.clipboard.contents == "bar", "Cmd+X writes to clipboard");
        check(h.text() == "foo ",            "Cmd+X removes selection");
    }

    // --- Cmd+V pastes at cursor, replacing selection ---
    {
        Harness h(64, "hello");
        h.clipboard.contents = " world";
        h.state.focused = true;
        h.state.cursor  = 5;
        h.events = {key_ev(ek::kV, ek::kModSuper)};
        h.draw();
        check(h.text() == "hello world",
              "Cmd+V inserts clipboard text at cursor");

        // Paste over selection.
        h.clear_input();
        h.state.cursor = 11;
        h.state.selection_anchor = 0;
        h.clipboard.contents = "hi";
        h.events = {key_ev(ek::kV, ek::kModSuper)};
        h.draw();
        check(h.text() == "hi", "Cmd+V replaces selection with clipboard text");
    }

    // --- Buffer-full: typing when strlen == capacity - 1 is a no-op ---
    {
        // capacity = 4 → max 3 chars + NUL. Pre-fill with "abc".
        Harness h(4, "abc");
        h.state.focused = true;
        h.state.cursor  = 3;
        h.events = {char_ev('d')};
        auto r = h.draw();
        check(!r.changed,          "typing into full buffer is a no-op");
        check(h.text() == "abc",   "buffer unchanged on overflow");
        check(h.state.cursor == 3, "cursor unchanged on overflow");
    }

    // --- CHAR events below 0x20 or above 0x7E are ignored (ASCII-only) ---
    {
        Harness h;
        h.state.focused = true;
        h.events = {
            char_ev(0x0A),   // newline
            char_ev(0x7F),   // DEL
            char_ev(0x00A0), // non-ASCII
            char_ev('a'),
        };
        h.draw();
        check(h.text() == "a", "only ASCII printable chars land in buffer");
    }

    // --- Unfocused field swallows no events ---
    {
        Harness h(64, "abc");
        // state.focused stays false; arrow events should NOT move cursor.
        h.state.cursor = 1;
        h.events = {key_ev(ek::kLeft), char_ev('X')};
        h.draw();
        check(h.state.cursor == 1,  "unfocused field ignores key events");
        check(h.text() == "abc",    "unfocused field ignores CHAR events");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
