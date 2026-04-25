// Unit tests for EditorWindowManager helpers — the pure-logic pieces the
// shipped manager composes directly. Exercises surface metrics, string-param
// views, and open/close bookkeeping without needing real GLFW/WGPU.
#include "runtime/core/editor_window_bookkeeping.h"

#include <cstring>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "=== Test: EditorWindowManager helpers ===\n\n");

    using vivid::EditorWindowBookkeeping;
    using vivid::EditorStringParamView;
    using vivid::EditorWindowSurfaceMetrics;
    using vivid::EditorShortcut;
    using vivid::classify_editor_shortcut;
    using vivid::make_editor_string_param_view;
    using vivid::make_editor_window_surface_metrics;

    // --- surface metrics preserve logical/editor coords while keeping
    // framebuffer size for WGPU surface config on HiDPI displays ---
    {
        EditorWindowSurfaceMetrics m =
            make_editor_window_surface_metrics(640, 360, 1280, 720, 2.0f);
        check(m.logical_width == 640, "logical width preserved");
        check(m.logical_height == 360, "logical height preserved");
        check(m.framebuffer_width == 1280, "framebuffer width preserved");
        check(m.framebuffer_height == 720, "framebuffer height preserved");
        check(std::fabs(m.dpi_scale - 2.0f) < 1e-6f, "dpi scale preserved");
    }

    // --- fallback from framebuffer size remains sensible when logical size
    // is not available yet ---
    {
        EditorWindowSurfaceMetrics m =
            make_editor_window_surface_metrics(0, 0, 1536, 960, 2.0f);
        check(m.logical_width == 768, "logical width derived from framebuffer/dpi");
        check(m.logical_height == 480, "logical height derived from framebuffer/dpi");
        check(m.framebuffer_width == 1536, "framebuffer width retained");
        check(m.framebuffer_height == 960, "framebuffer height retained");
    }

    // --- string-param view exposes the existing runtime-owned pointer array
    // without reordering or copying ---
    {
        std::vector<const char*> values = {"fixture.mov", "hello editor"};
        EditorStringParamView view = make_editor_string_param_view(values);
        check(view.count == 2, "string-param view count = 2");
        check(view.values != nullptr, "string-param view is non-null");
        check(std::strcmp(view.values[0], "fixture.mov") == 0,
              "string-param view preserves first entry order");
        check(std::strcmp(view.values[1], "hello editor") == 0,
              "string-param view preserves second entry order");
    }

    {
        std::vector<const char*> values;
        EditorStringParamView view = make_editor_string_param_view(values);
        check(view.count == 0, "empty string-param view count = 0");
        check(view.values == nullptr, "empty string-param view is null");
    }

    // --- editor shortcut classifier: Cmd+Z / Cmd+Shift+Z / Cmd+W routing ---
    {
        // Cmd+Z (press) → Undo when wants_keyboard is false.
        check(classify_editor_shortcut('Z', /*is_press*/true,
                                       /*cmd_or_ctrl*/true, /*shift*/false,
                                       /*wants_keyboard*/false)
                  == EditorShortcut::Undo,
              "Cmd+Z press → Undo");
        // Cmd+Shift+Z → Redo.
        check(classify_editor_shortcut('Z', true, true, true, false)
                  == EditorShortcut::Redo,
              "Cmd+Shift+Z press → Redo");
        // Cmd+W → Close (even mid-text-edit).
        check(classify_editor_shortcut('W', true, true, false, false)
                  == EditorShortcut::Close,
              "Cmd+W press → Close");
        check(classify_editor_shortcut('W', true, true, false, true)
                  == EditorShortcut::Close,
              "Cmd+W press → Close even when wants_keyboard=true");
        // wants_keyboard suppresses Cmd+Z but not Cmd+W.
        check(classify_editor_shortcut('Z', true, true, false, true)
                  == EditorShortcut::None,
              "Cmd+Z press with wants_keyboard=true → None (operator handles)");
        check(classify_editor_shortcut('Z', true, true, true, true)
                  == EditorShortcut::None,
              "Cmd+Shift+Z press with wants_keyboard=true → None");
        // Release events never trigger.
        check(classify_editor_shortcut('Z', /*is_press*/false, true, false, false)
                  == EditorShortcut::None,
              "Cmd+Z release → None (only press dispatches)");
        // Without Cmd/Ctrl, plain Z is a regular key.
        check(classify_editor_shortcut('Z', true, /*cmd_or_ctrl*/false, false, false)
                  == EditorShortcut::None,
              "plain Z press → None (no modifier)");
        // Other letters pass through.
        check(classify_editor_shortcut('A', true, true, false, false)
                  == EditorShortcut::None,
              "Cmd+A press → None (not a manager shortcut)");
    }

    // --- record_open / is_open / size ---
    {
        EditorWindowBookkeeping bk;
        check(bk.size() == 0, "initial: empty");
        check(!bk.is_open("a"), "initial: is_open(a) false");

        check(bk.record_open("a"), "record_open(a) returns true");
        check(bk.is_open("a"), "is_open(a) true after record");
        check(bk.size() == 1, "size 1 after first open");

        check(bk.record_open("b"), "record_open(b) returns true");
        check(bk.is_open("b"), "is_open(b) true");
        check(bk.size() == 2, "size 2 after second open");
    }

    // --- duplicate-open prevention ---
    {
        EditorWindowBookkeeping bk;
        bk.record_open("a");
        check(!bk.record_open("a"), "duplicate record_open(a) returns false");
        check(bk.size() == 1, "duplicate does not grow size");
    }

    // --- mark_for_destroy + sweep invokes destroyer and removes ---
    {
        EditorWindowBookkeeping bk;
        bk.record_open("a");
        bk.record_open("b");
        bk.record_open("c");
        bk.mark_for_destroy("b");

        std::vector<std::string> destroyed;
        auto destroyer = [&](const std::string& id) { destroyed.push_back(id); };
        size_t n = bk.sweep(destroyer);
        check(n == 1, "sweep destroyed 1 entry");
        check(destroyed.size() == 1 && destroyed[0] == "b",
              "destroyer invoked for 'b' exactly");
        check(!bk.is_open("b"), "b removed after sweep");
        check(bk.is_open("a"), "a preserved after sweep");
        check(bk.is_open("c"), "c preserved after sweep");
        check(bk.size() == 2, "size 2 after sweep");
    }

    // --- mark_for_destroy for unknown id is a no-op ---
    {
        EditorWindowBookkeeping bk;
        bk.record_open("a");
        bk.mark_for_destroy("nonexistent");
        int calls = 0;
        bk.sweep([&](const std::string&) { ++calls; });
        check(calls == 0, "mark_for_destroy on unknown id does not trigger destroyer");
        check(bk.is_open("a"), "unrelated entry preserved");
    }

    // --- close_all empties and invokes destroyer for every entry ---
    {
        EditorWindowBookkeeping bk;
        bk.record_open("x");
        bk.record_open("y");
        bk.record_open("z");

        std::unordered_map<std::string, int> call_counts;
        size_t n = bk.close_all([&](const std::string& id) { ++call_counts[id]; });
        check(n == 3, "close_all reports 3 destroyed");
        check(bk.size() == 0, "bookkeeping empty after close_all");
        check(!bk.is_open("x") && !bk.is_open("y") && !bk.is_open("z"),
              "all ids removed after close_all");
        check(call_counts["x"] == 1 && call_counts["y"] == 1 && call_counts["z"] == 1,
              "each destroyer invoked exactly once");
    }

    // --- close_one removes only the requested entry ---
    {
        EditorWindowBookkeeping bk;
        bk.record_open("a");
        bk.record_open("b");

        int destroyed_b = 0;
        check(bk.close_one("b", [&](const std::string&) { ++destroyed_b; }),
              "close_one(b) returns true");
        check(destroyed_b == 1, "destroyer invoked once for b");
        check(!bk.is_open("b"), "b removed");
        check(bk.is_open("a"), "a preserved");

        int destroyed_missing = 0;
        check(!bk.close_one("nonexistent",
                            [&](const std::string&) { ++destroyed_missing; }),
              "close_one(unknown) returns false");
        check(destroyed_missing == 0, "destroyer not invoked for unknown id");
    }

    // --- reload simulation: close_all then re-open works ---
    {
        EditorWindowBookkeeping bk;
        bk.record_open("drum_1");
        bk.record_open("mseg_1");
        int destroyed = 0;
        bk.close_all([&](const std::string&) { ++destroyed; });
        check(destroyed == 2, "reload closes both editors");
        check(bk.size() == 0, "empty after reload");

        // After reload, re-opening is allowed.
        check(bk.record_open("drum_1"), "re-open after reload succeeds");
        check(bk.is_open("drum_1"), "re-opened entry present");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
