#include "control/midi_clip/midi_clip_core.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include "test_helpers.h"

static void push_be16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
}

static void push_be32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
}

static void push_varlen(std::vector<uint8_t>& out, uint32_t value) {
    uint8_t bytes[5] = {};
    int count = 1;
    bytes[4] = static_cast<uint8_t>(value & 0x7Fu);
    while ((value >>= 7u) != 0) {
        bytes[4 - count] = static_cast<uint8_t>((value & 0x7Fu) | 0x80u);
        ++count;
    }
    for (int i = 5 - count; i < 5; ++i) out.push_back(bytes[i]);
}

static std::filesystem::path write_test_midi(const std::filesystem::path& path,
                                             uint32_t note_off_ticks,
                                             uint32_t duration_ticks) {
    std::vector<uint8_t> track;
    push_varlen(track, 0);
    track.insert(track.end(), {0xFF, 0x51, 0x03, 0x01, 0x86, 0xA0});
    push_varlen(track, 0);
    track.insert(track.end(), {0x90, 60, 100});
    push_varlen(track, note_off_ticks);
    track.insert(track.end(), {0x80, 60, 0});
    push_varlen(track, duration_ticks > note_off_ticks ? duration_ticks - note_off_ticks : 0);
    track.insert(track.end(), {0xFF, 0x2F, 0x00});

    std::vector<uint8_t> midi;
    midi.insert(midi.end(), {'M', 'T', 'h', 'd'});
    push_be32(midi, 6);
    push_be16(midi, 0);
    push_be16(midi, 1);
    push_be16(midi, 100);
    midi.insert(midi.end(), {'M', 'T', 'r', 'k'});
    push_be32(midi, static_cast<uint32_t>(track.size()));
    midi.insert(midi.end(), track.begin(), track.end());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(midi.data()), static_cast<std::streamsize>(midi.size()));
    return path;
}

static std::filesystem::path write_overlap_test_midi(const std::filesystem::path& path) {
    std::vector<uint8_t> track;
    push_varlen(track, 0);
    track.insert(track.end(), {0xFF, 0x51, 0x03, 0x01, 0x86, 0xA0});
    push_varlen(track, 0);
    track.insert(track.end(), {0x90, 60, 100});
    push_varlen(track, 2);
    track.insert(track.end(), {0x90, 60, 80});
    push_varlen(track, 2);
    track.insert(track.end(), {0x80, 60, 0});
    push_varlen(track, 2);
    track.insert(track.end(), {0x80, 60, 0});
    push_varlen(track, 2);
    track.insert(track.end(), {0xFF, 0x2F, 0x00});

    std::vector<uint8_t> midi;
    midi.insert(midi.end(), {'M', 'T', 'h', 'd'});
    push_be32(midi, 6);
    push_be16(midi, 0);
    push_be16(midi, 1);
    push_be16(midi, 100);
    midi.insert(midi.end(), {'M', 'T', 'r', 'k'});
    push_be32(midi, static_cast<uint32_t>(track.size()));
    midi.insert(midi.end(), track.begin(), track.end());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(midi.data()), static_cast<std::streamsize>(midi.size()));
    return path;
}

static VividAudioContext make_ctx(uint32_t buffer_size,
                                  uint32_t sample_rate,
                                  void** custom_outputs) {
    VividAudioContext ctx{};
    ctx.buffer_size = buffer_size;
    ctx.sample_rate = sample_rate;
    ctx.metronome_bpm = 120.0f;
    ctx.metronome_beats_per_bar = 4.0f;
    ctx.custom_outputs = custom_outputs;
    ctx.custom_output_count = 1;
    return ctx;
}

static void noop_rect(void*, float, float, float, float, VividColor) {}
static void noop_round(void*, float, float, float, float, float, VividColor) {}
static void noop_line(void*, float, float, float, float, float, VividColor) {}
static float text_width(void*, const char* s, float scale) {
    return s ? static_cast<float>(std::strlen(s)) * 8.0f * scale : 0.0f;
}
static float line_height(void*) { return 14.0f; }
static void noop_clip(void*, float, float, float, float) {}
static void noop_pop(void*) {}

struct EditorCapture {
    struct TextDraw {
        std::string text;
        float x = 0.0f;
        float y = 0.0f;
        float scale = 1.0f;
    };
    struct Widget {
        std::string kind;
        std::string label;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        int cols = 0;
        int int_value = 0;
        unsigned flags = 0;
    };
    bool pattern_data_written = false;
    size_t pattern_data_len = 0;
    std::string pattern_data;
    bool clip_data_ref_written = false;
    std::string clip_data_ref;
    bool file_cleared = false;
    int set_param_count = 0;
    std::vector<std::string> param_names;
    std::vector<float> param_values;
    std::vector<TextDraw> texts;
    std::vector<Widget> widgets;
};

static void capture_text(void* opaque, float x, float y, const char* text,
                         VividColor, float scale) {
    auto* cap = static_cast<EditorCapture*>(opaque);
    if (!cap || !text) return;
    cap->texts.push_back({text, x, y, scale});
}

static void capture_set_string(void* opaque, const char* name, const char* value) {
    auto* cap = static_cast<EditorCapture*>(opaque);
    if (std::strcmp(name, "pattern_data") == 0) {
        cap->pattern_data_written = true;
        cap->pattern_data_len = value ? std::strlen(value) : 0;
        cap->pattern_data = value ? value : "";
    } else if (std::strcmp(name, "clip_data_ref") == 0) {
        cap->clip_data_ref_written = true;
        cap->clip_data_ref = value ? value : "";
    } else if (std::strcmp(name, "file") == 0) {
        cap->file_cleared = !value || value[0] == '\0';
    }
}

static void capture_set_param(void* opaque, const char* name, float value) {
    auto* cap = static_cast<EditorCapture*>(opaque);
    ++cap->set_param_count;
    cap->param_names.push_back(name ? name : "");
    cap->param_values.push_back(value);
}

static void capture_widget(void* opaque, const VividIntrospectWidget* w) {
    auto* cap = static_cast<EditorCapture*>(opaque);
    if (!cap || !w) return;
    EditorCapture::Widget out;
    out.kind = w->kind ? w->kind : "";
    out.label = w->label ? w->label : "";
    out.x = w->x;
    out.y = w->y;
    out.w = w->w;
    out.h = w->h;
    out.cols = w->cols;
    out.int_value = w->int_value;
    out.flags = w->flags;
    cap->widgets.push_back(out);
}

static VividEditorContext make_editor_ctx(EditorCapture& cap,
                                          const char* const* strings,
                                          float* params) {
    VividEditorContext ctx{};
    ctx.surface_width = 1100.0f;
    ctx.surface_height = 700.0f;
    ctx.dpi_scale = 1.0f;
    ctx.draw.opaque = &cap;
    ctx.draw.draw_rect = noop_rect;
    ctx.draw.draw_rounded_rect = noop_round;
    ctx.draw.draw_text = capture_text;
    ctx.draw.draw_line = noop_line;
    ctx.draw.text_width = text_width;
    ctx.draw.line_height = line_height;
    ctx.draw.push_clip_rect = noop_clip;
    ctx.draw.pop_clip_rect = noop_pop;
    ctx.commands.opaque = &cap;
    ctx.commands.set_string_param = capture_set_string;
    ctx.commands.set_param = capture_set_param;
    ctx.param_values = params;
    ctx.param_count = 14;
    ctx.output_values = nullptr;
    ctx.output_count = 0;
    ctx.string_param_values = strings;
    ctx.string_param_count = 5;
    ctx.introspect_sink = &cap;
    ctx.introspect_fn = capture_widget;
    return ctx;
}

static bool drew_text_containing(const EditorCapture& cap, const std::string& needle) {
    for (const auto& t : cap.texts) {
        if (t.text.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

static std::optional<EditorCapture::Widget> find_widget(
        const EditorCapture& cap,
        const std::string& kind,
        const std::string& label) {
    for (const auto& w : cap.widgets) {
        if (w.kind == kind && w.label == label)
            return w;
    }
    return std::nullopt;
}

static std::optional<EditorCapture::Widget> find_radio(
        const EditorCapture& cap, int cols) {
    for (const auto& w : cap.widgets) {
        if (w.kind == "radio" && w.cols == cols)
            return w;
    }
    return std::nullopt;
}

static bool param_written(const EditorCapture& cap, const std::string& name) {
    for (const auto& n : cap.param_names) {
        if (n == name) return true;
    }
    return false;
}

static bool has_widget_kind(const EditorCapture& cap, const std::string& kind) {
    for (const auto& w : cap.widgets) {
        if (w.kind == kind) return true;
    }
    return false;
}

static std::optional<EditorCapture::Widget> find_toolbar_section(
        const EditorCapture& cap,
        const std::string& label) {
    for (const auto& w : cap.widgets) {
        if (w.kind == "toolbar_section" && w.label == label)
            return w;
    }
    return std::nullopt;
}

static bool rects_overlap(const EditorCapture::Widget& a,
                          const EditorCapture::Widget& b) {
    constexpr float eps = 0.25f;
    return a.x + a.w > b.x + eps && b.x + b.w > a.x + eps &&
           a.y + a.h > b.y + eps && b.y + b.h > a.y + eps;
}

static bool toolbar_sections_non_overlapping(const EditorCapture& cap) {
    std::vector<EditorCapture::Widget> sections;
    for (const auto& w : cap.widgets) {
        if (w.kind == "toolbar_section")
            sections.push_back(w);
    }
    for (size_t i = 0; i < sections.size(); ++i) {
        for (size_t j = i + 1; j < sections.size(); ++j) {
            if (rects_overlap(sections[i], sections[j]))
                return false;
        }
    }
    return true;
}

static std::array<float, 14> make_editor_params(float length_bars = 2.0f) {
    return {
        length_bars, // length_bars
        1.0f,        // quantize_grid: 1/16
        1.0f,        // playing
        0.0f,        // loop
        0.0f,        // transpose
        1.0f,        // velocity_scale
        0.0f,        // loop_start_beat
        0.0f,        // loop_end_beat
        0.0f,        // _editor_fold
        -1.0f,       // _editor_scale_root
        0.0f,        // _editor_scale_type
        0.0f,        // _editor_zoom_beats
        0.0f,        // _editor_scroll_beat
        14.0f        // _editor_row_height
    };
}

static std::vector<midi_clip::ParsedNote> parse_notes(const std::string& s) {
    std::vector<midi_clip::ParsedNote> notes;
    midi_clip::parse_pattern(s, notes);
    return notes;
}

static void set_mouse(VividEditorContext& ctx, float x, float y,
                      bool down, bool clicked = false,
                      bool right_clicked = false,
                      bool shift = false) {
    ctx.mouse.x = x;
    ctx.mouse.y = y;
    ctx.mouse.prev_x = x;
    ctx.mouse.prev_y = y;
    ctx.mouse.left_down = down ? 1 : 0;
    ctx.mouse.left_clicked = clicked ? 1 : 0;
    ctx.mouse.right_clicked = right_clicked ? 1 : 0;
    ctx.mouse.shift_down = shift ? 1 : 0;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path sandbox = fs::path("./.test_midi_clip");
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    const auto midi_path = write_test_midi(sandbox / "clip.mid", 8, 10);
    const auto held_midi_path = write_test_midi(sandbox / "held.mid", 32, 40);
    const auto one_beat_midi_path = write_test_midi(sandbox / "one_beat.mid", 100, 120);
    const auto overlap_midi_path = write_overlap_test_midi(sandbox / "overlap.mid");

    {
        MidiClipCore op;
        auto fixture = fs::path(VIVID_SOURCE_DIR) / "assets" / "sweelinck.mid";
        if (fs::exists(fixture)) {
            op.file.str_value = fixture.string();
            op.main_thread_update(0.0);
            check(op.file_note_count_ == 2658,
                  "MidiClip parses expected Sweelinck note count");
            check_float(static_cast<float>(op.clip_length_beats_), 788.0f, 1e-4f,
                        "MidiClip computes Sweelinck beat length");
            check_float(static_cast<float>(op.clip_length_bars_), 197.0f, 1e-4f,
                        "MidiClip computes Sweelinck bar length");
        } else {
            std::fprintf(stderr, "SKIP: sweelinck.mid fixture missing\n");
        }
    }

    {
        MidiClipCore op;
        op.file.str_value = midi_path.string();
        op.transpose.value = 12.0f;
        op.velocity_scale.value = 0.5f;
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(64, 1000, custom_outputs);
        op.process_audio(&ctx);

        check(custom_outputs[0] != nullptr, "MidiClip publishes notes_out for file-backed playback");
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        check(notes->count >= 2, "MidiClip emits NOTE_ON and NOTE_OFF from file");
        if (notes->count >= 2) {
            check(notes->events[0].type == VIVID_NOTE_ON, "MidiClip first event is NOTE_ON");
            check(notes->events[0].note_number == 72, "MidiClip transpose applies to file playback");
            check_float(notes->events[0].value, 50.0f / 127.0f, 1e-4f,
                        "MidiClip velocity_scale applies to file playback");
            check(notes->events[1].type == VIVID_NOTE_OFF, "MidiClip second event is NOTE_OFF");
            check(notes->events[1].note_id == notes->events[0].note_id,
                  "MidiClip NOTE_OFF matches NOTE_ON id");
        }
    }

    {
        MidiClipCore op;
        op.file.str_value = one_beat_midi_path.string();
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(64, 1000, custom_outputs);
        ctx.metronome_beats_elapsed = 10000.0;
        op.process_audio(&ctx);

        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        bool note_on_seen = false;
        for (uint32_t i = 0; notes && i < notes->count; ++i)
            if (notes->events[i].type == VIVID_NOTE_ON) note_on_seen = true;
        check(note_on_seen,
              "MidiClip file playback starts at clip beat 0, independent of global graph beat");
    }

    {
        MidiClipCore op;
        op.file.str_value = one_beat_midi_path.string();
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(64, 1000, custom_outputs);
        op.process_audio(&ctx);
        op.playing.value = 0.0f;
        op.process_audio(&ctx);
        op.playing.value = 1.0f;
        ctx.metronome_beats_elapsed = 512.0;
        op.process_audio(&ctx);

        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        bool restarted = false;
        for (uint32_t i = 0; notes && i < notes->count; ++i)
            if (notes->events[i].type == VIVID_NOTE_ON) restarted = true;
        check(restarted, "MidiClip playing off/on restarts file clips at beat 0");
    }

    {
        MidiClipCore op;
        op.file.str_value = midi_path.string();
        op.loop.value = 1.0f;
        op.loop_end_beat.value = 0.1f;
        op.main_thread_update(0.0);
        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(128, 1000, custom_outputs);
        op.process_audio(&ctx);
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        int note_on_seen = 0;
        for (uint32_t i = 0; i < notes->count; ++i)
            if (notes->events[i].type == VIVID_NOTE_ON) ++note_on_seen;
        check(note_on_seen >= 2, "MidiClip loops within the same audio block");
    }

    {
        MidiClipCore slow;
        slow.file.str_value = one_beat_midi_path.string();
        slow.main_thread_update(0.0);
        void* slow_outputs[1] = {nullptr};
        auto slow_ctx = make_ctx(500, 1000, slow_outputs);
        slow_ctx.metronome_bpm = 60.0f;
        slow.process_audio(&slow_ctx);
        auto* slow_notes = static_cast<VividNoteBuffer*>(slow_outputs[0]);

        MidiClipCore fast;
        fast.file.str_value = one_beat_midi_path.string();
        fast.main_thread_update(0.0);
        void* fast_outputs[1] = {nullptr};
        auto fast_ctx = make_ctx(500, 1000, fast_outputs);
        fast_ctx.metronome_bpm = 240.0f;
        fast.process_audio(&fast_ctx);
        auto* fast_notes = static_cast<VividNoteBuffer*>(fast_outputs[0]);

        bool slow_off = false;
        for (uint32_t i = 0; slow_notes && i < slow_notes->count; ++i)
            if (slow_notes->events[i].type == VIVID_NOTE_OFF) slow_off = true;
        bool fast_off = false;
        for (uint32_t i = 0; fast_notes && i < fast_notes->count; ++i)
            if (fast_notes->events[i].type == VIVID_NOTE_OFF) fast_off = true;
        check(!slow_off && fast_off,
              "MidiClip imported MIDI follows graph tempo after beat conversion");
    }

    {
        MidiClipCore op;
        op.file.str_value = held_midi_path.string();
        op.main_thread_update(0.0);
        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(64, 1000, custom_outputs);
        op.process_audio(&ctx);
        op.playing.value = 0.0f;
        op.process_audio(&ctx);
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        bool saw_off = false;
        for (uint32_t i = 0; i < notes->count; ++i)
            if (notes->events[i].type == VIVID_NOTE_OFF) saw_off = true;
        check(saw_off, "MidiClip stopping playback flushes active notes");
    }

    {
        MidiClipCore op;
        op.file.str_value = overlap_midi_path.string();
        op.main_thread_update(0.0);
        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(64, 1000, custom_outputs);
        op.process_audio(&ctx);
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        uint64_t first_on = 0, second_on = 0, first_off = 0, second_off = 0;
        int on_count = 0, off_count = 0;
        for (uint32_t i = 0; i < notes->count; ++i) {
            const auto& e = notes->events[i];
            if (e.type == VIVID_NOTE_ON) {
                if (on_count == 0) first_on = e.note_id;
                if (on_count == 1) second_on = e.note_id;
                ++on_count;
            } else if (e.type == VIVID_NOTE_OFF) {
                if (off_count == 0) first_off = e.note_id;
                if (off_count == 1) second_off = e.note_id;
                ++off_count;
            }
        }
        check(on_count == 2 && off_count == 2, "MidiClip preserves overlapping same-pitch events");
        check(first_on != 0 && second_on != 0 && first_on != second_on,
              "MidiClip allocates distinct ids for overlaps");
        check(first_off == first_on && second_off == second_on,
              "MidiClip releases overlapping notes FIFO");
    }

    {
        MidiClipCore op;
        op.inject_events({{0x90, 64, 100}});
        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        check(notes && notes->count == 1 && notes->events[0].note_number == 64,
              "MidiClip inject hook path emits notes_out without a loaded file");
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 0.5f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f,
                  true, true);
        op.draw_editor(&ectx);
        auto notes = parse_notes(cap.pattern_data);
        check(cap.pattern_data_written, "MidiClip empty left-click writes pattern_data immediately");
        check(notes.size() == 1, "MidiClip empty left-click adds one note");
        if (!notes.empty()) {
            check(notes[0].pitch == 96, "MidiClip empty left-click chooses clicked pitch");
            check_float(static_cast<float>(notes[0].start_beat), 0.5f, 1e-4f,
                        "MidiClip empty left-click snaps beat");
        }
        check(op.drag_mode_ == MidiClipCore::DragMode::AddingNote,
              "MidiClip empty left-click starts add/resize gesture, not box select");
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);
        op.editor_zoom_beats_ = 4.0f;
        op.editor_scroll_x_ = 3.0f;
        op.draw_editor(&ectx);
        check_float(op.editor_scroll_x_, 3.0f, 1e-4f,
                    "MidiClip editor keeps horizontal scroll after initial param sync");
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 0.25f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f,
                  true, true, false, true);
        op.draw_editor(&ectx);
        check(!cap.pattern_data_written, "MidiClip shift empty drag does not add note immediately");
        check(op.drag_mode_ == MidiClipCore::DragMode::BoxSelect,
              "MidiClip shift empty drag starts box select");
    }

    {
        MidiClipCore op;
        std::string pattern =
            R"([{"p":96,"s":0.5,"d":0.25,"v":0.8},{"p":97,"s":1.0,"d":0.25,"v":0.8}])";
        auto params = make_editor_params();
        const char* strings[5] = {pattern.c_str(), "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 0.55f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f,
                  true, true);
        op.draw_editor(&ectx);
        check(op.note_selected_.size() == 2 && op.note_selected_[0] && !op.note_selected_[1],
              "MidiClip note left-click exclusively selects clicked note");
    }

    {
        MidiClipCore op;
        std::string pattern = R"([{"p":96,"s":0.5,"d":0.25,"v":0.8}])";
        auto params = make_editor_params();
        const char* strings[5] = {pattern.c_str(), "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 0.55f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f,
                  false, false, true);
        op.draw_editor(&ectx);
        auto notes = parse_notes(cap.pattern_data);
        check(cap.pattern_data_written && notes.empty(),
              "MidiClip right-click on note deletes it");
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        const float y = 78.0f + 31.0f * 14.0f + 7.0f;
        set_mouse(ectx, 52.0f + 0.5f * 129.5f, y, true, true);
        op.draw_editor(&ectx);
        op.pattern_data.str_value = cap.pattern_data;
        strings[0] = op.pattern_data.str_value.c_str();
        cap.pattern_data_written = false;
        ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 1.5f * 129.5f, y, true, false);
        op.draw_editor(&ectx);
        auto notes = parse_notes(cap.pattern_data);
        check(!notes.empty() && notes[0].duration_beats > 0.25,
              "MidiClip drag-created note extends duration");
    }

    {
        MidiClipCore op;
        auto fixture = fs::path(VIVID_SOURCE_DIR) / "assets" / "sweelinck.mid";
        if (fs::exists(fixture)) {
            op.file.str_value = fixture.string();
            op.main_thread_update(0.0);
            auto params = make_editor_params(op.length_bars.value);
            const char* strings[5] = {"[]", op.file.str_value.c_str(), "", "", ""};
            EditorCapture cap;
            auto ectx = make_editor_ctx(cap, strings, params.data());
            op.draw_editor(&ectx);
            check(!cap.pattern_data_written,
                  "Opening Sweelinck in MidiClip editor does not serialize pattern_data");
            check(find_toolbar_section(cap, "CLIP").has_value() &&
                  find_toolbar_section(cap, "TIME").has_value() &&
                  find_toolbar_section(cap, "MUSIC").has_value() &&
                  find_toolbar_section(cap, "NAV").has_value() &&
                  find_toolbar_section(cap, "ACTIONS").has_value(),
                  "MidiClip toolbar exposes grouped section introspection");
            check(toolbar_sections_non_overlapping(cap),
                  "MidiClip toolbar sections do not overlap at default width");
            check(drew_text_containing(cap, "sweelinck.mid"),
                  "MidiClip toolbar shows file-backed source name");
            check(drew_text_containing(cap, "197 bars"),
                  "MidiClip toolbar shows read-only parsed Sweelinck length");
            check(drew_text_containing(cap, "2658 notes"),
                  "MidiClip toolbar shows Sweelinck note count");
            check(!has_widget_kind(cap, "text_field"),
                  "MidiClip toolbar does not expose editable length field for file-backed clips");
        } else {
            std::fprintf(stderr, "SKIP: sweelinck.mid fixture missing\n");
        }
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);
        check(has_widget_kind(cap, "text_field"),
              "MidiClip toolbar keeps authored clip length editable");
        check(drew_text_containing(cap, "Authored"),
              "MidiClip toolbar labels authored clips");
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        ectx.surface_width = 720.0f;
        op.draw_editor(&ectx);
        check(toolbar_sections_non_overlapping(cap),
              "MidiClip toolbar sections do not overlap at narrow width");
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);
        auto radio = find_radio(cap, 4);
        check(radio.has_value(), "MidiClip toolbar exposes snap grid segmented control");
        if (radio) {
            cap = EditorCapture{};
            ectx = make_editor_ctx(cap, strings, params.data());
            set_mouse(ectx, radio->x + radio->w * 0.625f, radio->y + radio->h * 0.5f,
                      false, true);
            op.draw_editor(&ectx);
            check(param_written(cap, "quantize_grid"),
                  "MidiClip toolbar grid selection writes quantize_grid");
        }
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);

        auto fold = find_widget(cap, "toggle", "Fold");
        check(fold.has_value(), "MidiClip toolbar exposes Fold as a toggle");
        if (fold) {
            cap = EditorCapture{};
            ectx = make_editor_ctx(cap, strings, params.data());
            set_mouse(ectx, fold->x + fold->w * 0.5f, fold->y + fold->h * 0.5f,
                      false, true);
            op.draw_editor(&ectx);
            check(param_written(cap, "_editor_fold"),
                  "MidiClip toolbar Fold toggle persists to hidden editor param");
        }
    }

    {
        MidiClipCore op;
        auto params = make_editor_params();
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);

        auto root = find_widget(cap, "button", "Root -");
        auto type = find_widget(cap, "button", "Maj");
        check(root.has_value() && type.has_value(),
              "MidiClip toolbar exposes explicit scale root and type controls");
        if (root) {
            cap = EditorCapture{};
            ectx = make_editor_ctx(cap, strings, params.data());
            set_mouse(ectx, root->x + root->w * 0.5f, root->y + root->h * 0.5f,
                      false, true);
            op.draw_editor(&ectx);
            check(op.scale_root_ == 0,
                  "MidiClip toolbar root button enables C without right-click behavior");
        }
        if (type) {
            cap = EditorCapture{};
            ectx = make_editor_ctx(cap, strings, params.data());
            set_mouse(ectx, type->x + type->w * 0.5f, type->y + type->h * 0.5f,
                      false, true);
            op.draw_editor(&ectx);
            check(op.scale_type_ == 1,
                  "MidiClip toolbar type button cycles scale type directly");
        }
    }

    {
        MidiClipCore op;
        auto params = make_editor_params(8.0f);
        const char* strings[5] = {"[]", "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);

        auto zoom_in = find_widget(cap, "button", "In");
        auto fit = find_widget(cap, "button", "Fit");
        check(zoom_in.has_value() && fit.has_value() && drew_text_containing(cap, "Bars 1-8 / 8"),
              "MidiClip toolbar exposes zoom controls and visible bar range");
        if (zoom_in) {
            cap = EditorCapture{};
            ectx = make_editor_ctx(cap, strings, params.data());
            set_mouse(ectx, zoom_in->x + zoom_in->w * 0.5f, zoom_in->y + zoom_in->h * 0.5f,
                      false, true);
            op.draw_editor(&ectx);
            check(param_written(cap, "_editor_zoom_beats"),
                  "MidiClip toolbar zoom in persists changed zoom");
            params[11] = op.editor_zoom_beats_;
            op.editor_zoom_beat_.value = op.editor_zoom_beats_;
        }
        if (fit) {
            cap = EditorCapture{};
            ectx = make_editor_ctx(cap, strings, params.data());
            set_mouse(ectx, fit->x + fit->w * 0.5f, fit->y + fit->h * 0.5f,
                      false, true);
            op.draw_editor(&ectx);
            check(param_written(cap, "_editor_zoom_beats"),
                  "MidiClip toolbar Fit persists zoom reset");
        }
    }

    {
        MidiClipCore op;
        std::string pattern = R"([{"p":96,"s":0.5,"d":0.25,"v":0.8}])";
        auto params = make_editor_params();
        const char* strings[5] = {pattern.c_str(), "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);

        auto actions = find_widget(cap, "button", "Actions");
        check(actions.has_value(), "MidiClip toolbar hides destructive commands behind Actions");
        if (actions) {
            cap = EditorCapture{};
            ectx = make_editor_ctx(cap, strings, params.data());
            set_mouse(ectx, actions->x + actions->w * 0.5f, actions->y + actions->h * 0.5f,
                      false, true);
            op.draw_editor(&ectx);
            check(!cap.pattern_data_written,
                  "MidiClip toolbar Actions click does not clear clip data");
        }
    }

    {
        MidiClipCore op;
        op.file.str_value = one_beat_midi_path.string();
        op.clip_data_ref_.str_value = (sandbox / "edited_long_clip.mclip.json").string();
        op.main_thread_update(0.0);
        auto params = make_editor_params(op.length_bars.value);
        const char* strings[5] = {"[]", op.file.str_value.c_str(), "", "",
                                  op.clip_data_ref_.str_value.c_str()};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        op.draw_editor(&ectx);

        set_mouse(ectx, 52.0f + 1.5f * 129.5f, 64.0f + 14.0f + (127 - 100) * 14.0f + 2.0f,
                  true, true);
        op.draw_editor(&ectx);

        check(cap.clip_data_ref_written && fs::exists(cap.clip_data_ref),
              "Editing file-backed MidiClip writes editable clip sidecar");
        check(cap.pattern_data == "[]",
              "Editing file-backed MidiClip keeps pattern_data compact");
        check(cap.file_cleared,
              "Editing file-backed MidiClip clears authoritative file param");

        MidiClipCore loaded;
        loaded.clip_data_ref_.str_value = cap.clip_data_ref;
        loaded.main_thread_update(0.0);
        void* custom_outputs[1] = {nullptr};
        auto actx = make_ctx(64, 1000, custom_outputs);
        loaded.process_audio(&actx);
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        check(notes && notes->count > 0,
              "MidiClip reloads clip_data_ref sidecar for playback");
    }

    // -----------------------------------------------------------------------
    // Undo / redo stack tests
    // -----------------------------------------------------------------------

    namespace ek = ::vivid::editor_keys;

    auto key_ev = [](int k, int mods = 0) {
        VividEditorEvent e{};
        e.type      = VIVID_EDITOR_EVENT_KEY;
        e.key       = k;
        e.action    = ek::kPress;
        e.modifiers = mods;
        return e;
    };

    // Helper: single draw_editor call with a set of key events
    auto draw_with_keys = [&](MidiClipCore& op, EditorCapture& cap,
                              const char* const* strings, float* params,
                              std::vector<VividEditorEvent> events) {
        cap = EditorCapture{};
        auto ectx = make_editor_ctx(cap, strings, params);
        ectx.events      = events.empty() ? nullptr : events.data();
        ectx.event_count = static_cast<uint32_t>(events.size());
        op.draw_editor(&ectx);
    };

    {
        // Undo add: add one note via left-click, Cmd+Z → note disappears
        MidiClipCore op;
        auto params = make_editor_params();
        std::string pat = "[]";
        const char* strings[5] = {pat.c_str(), "", "", "", ""};
        EditorCapture cap;
        // Click to add note
        auto ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 0.5f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f, true, true);
        op.draw_editor(&ectx);
        pat = cap.pattern_data;
        strings[0] = pat.c_str();
        check(!op.undo_stack_.empty(), "MidiClip push_undo_snapshot records add-note action");
        // Cmd+Z
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kZ, ek::kModSuper)});
        auto notes = parse_notes(cap.pattern_data);
        check(cap.pattern_data_written && notes.empty(),
              "MidiClip Cmd+Z undoes note addition");
        check(op.undo_stack_.empty(),
              "MidiClip undo stack is empty after undoing last action");
        check(!op.redo_stack_.empty(),
              "MidiClip redo stack has one entry after undo");
    }

    {
        // Redo: add note, undo, Cmd+Shift+Z → note returns
        MidiClipCore op;
        auto params = make_editor_params();
        std::string pat = "[]";
        const char* strings[5] = {pat.c_str(), "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 0.5f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f, true, true);
        op.draw_editor(&ectx);
        pat = cap.pattern_data;
        strings[0] = pat.c_str();
        // Undo
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kZ, ek::kModSuper)});
        pat = cap.pattern_data;
        strings[0] = pat.c_str();
        // Redo
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kZ, ek::kModSuper | ek::kModShift)});
        auto notes = parse_notes(cap.pattern_data);
        check(cap.pattern_data_written && notes.size() == 1,
              "MidiClip Cmd+Shift+Z redoes note addition");
    }

    {
        // Redo cleared on new action: add note, undo, add second note → redo_stack_ empty
        MidiClipCore op;
        auto params = make_editor_params();
        std::string pat = "[]";
        const char* strings[5] = {pat.c_str(), "", "", "", ""};
        EditorCapture cap;
        auto ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 0.5f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f, true, true);
        op.draw_editor(&ectx);
        pat = cap.pattern_data;
        strings[0] = pat.c_str();
        // Undo
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kZ, ek::kModSuper)});
        pat = cap.pattern_data;
        strings[0] = pat.c_str();
        // New action: add a note at a different beat
        ectx = make_editor_ctx(cap, strings, params.data());
        set_mouse(ectx, 52.0f + 1.5f * 129.5f, 78.0f + 31.0f * 14.0f + 7.0f, true, true);
        op.draw_editor(&ectx);
        check(op.redo_stack_.empty(),
              "MidiClip new action after undo clears redo stack");
    }

    {
        // Undo delete: add note, delete via Delete key, undo → note returns
        MidiClipCore op;
        std::string pat = R"([{"p":96,"s":0.5,"d":0.25,"v":0.8}])";
        auto params = make_editor_params();
        const char* strings[5] = {pat.c_str(), "", "", "", ""};
        EditorCapture cap;
        // Select all, then delete
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kA, ek::kModSuper),
                        key_ev(ek::kDelete)});
        pat = cap.pattern_data;
        strings[0] = pat.c_str();
        check(parse_notes(cap.pattern_data).empty(),
              "MidiClip Delete key removes selected notes");
        // Undo
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kZ, ek::kModSuper)});
        auto notes = parse_notes(cap.pattern_data);
        check(cap.pattern_data_written && notes.size() == 1,
              "MidiClip Cmd+Z restores deleted note");
    }

    {
        // Undo stack depth: push more than kMaxUndoDepth snapshots, verify cap
        MidiClipCore op;
        for (int i = 0; i < MidiClipCore::kMaxUndoDepth + 5; ++i) {
            op.push_undo_snapshot();
        }
        check(static_cast<int>(op.undo_stack_.size()) == MidiClipCore::kMaxUndoDepth,
              "MidiClip undo stack is capped at kMaxUndoDepth entries");
    }

    // -----------------------------------------------------------------------
    // Paste cursor tests
    // -----------------------------------------------------------------------

    {
        // Copy sets paste cursor to end of selection
        MidiClipCore op;
        std::string pat = R"([{"p":96,"s":0.0,"d":1.0,"v":0.8},{"p":97,"s":1.0,"d":0.5,"v":0.8}])";
        auto params = make_editor_params();
        const char* strings[5] = {pat.c_str(), "", "", "", ""};
        EditorCapture cap;
        // Select all, then Cmd+C
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kA, ek::kModSuper),
                        key_ev(ek::kC, ek::kModSuper)});
        check(op.paste_cursor_beat_ == 1.5,
              "MidiClip Cmd+C sets paste cursor to end of copied selection");
    }

    {
        // Paste at cursor, not scroll position
        MidiClipCore op;
        std::string pat = R"([{"p":96,"s":0.0,"d":0.25,"v":0.8}])";
        auto params = make_editor_params(4.0f);  // 4-bar pattern
        const char* strings[5] = {pat.c_str(), "", "", "", ""};
        EditorCapture cap;
        // Select all and copy → paste cursor = 0.25
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kA, ek::kModSuper),
                        key_ev(ek::kC, ek::kModSuper)});
        // Move paste cursor to beat 4.0 manually
        op.paste_cursor_beat_ = 4.0;
        // Paste
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kV, ek::kModSuper)});
        auto notes = parse_notes(cap.pattern_data);
        bool pasted_at_cursor = false;
        for (const auto& n : notes)
            if (std::fabs(n.start_beat - 4.0) < 0.01) pasted_at_cursor = true;
        check(pasted_at_cursor,
              "MidiClip Cmd+V pastes at paste_cursor_beat_ when set");
    }

    {
        // Paste advances cursor by clipboard span
        MidiClipCore op;
        std::string pat = R"([{"p":96,"s":0.0,"d":1.0,"v":0.8}])";
        auto params = make_editor_params(8.0f);
        const char* strings[5] = {pat.c_str(), "", "", "", ""};
        EditorCapture cap;
        // Select all and copy (1-beat note → cursor = 1.0)
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kA, ek::kModSuper),
                        key_ev(ek::kC, ek::kModSuper)});
        check_float(static_cast<float>(op.paste_cursor_beat_), 1.0f, 1e-4f,
                    "MidiClip Cmd+C sets paste cursor to 1.0 after copying 1-beat note");
        // First paste: cursor moves to 2.0
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kV, ek::kModSuper)});
        check_float(static_cast<float>(op.paste_cursor_beat_), 2.0f, 0.01f,
                    "MidiClip first paste advances cursor by clipboard span");
        // Second paste: cursor moves to 3.0
        pat = cap.pattern_data;
        strings[0] = pat.c_str();
        draw_with_keys(op, cap, strings, params.data(),
                       {key_ev(ek::kV, ek::kModSuper)});
        check_float(static_cast<float>(op.paste_cursor_beat_), 3.0f, 0.01f,
                    "MidiClip second paste advances cursor to 3.0 (chain paste)");
    }

    fs::remove_all(sandbox);
    std::fprintf(stderr, "\n%d failed\n", failures);
    return failures ? 1 : 0;
}
