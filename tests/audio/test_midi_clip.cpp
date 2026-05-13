#include "control/midi_clip/midi_clip_core.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
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
static void noop_text(void*, float, float, const char*, VividColor, float) {}
static void noop_line(void*, float, float, float, float, float, VividColor) {}
static float text_width(void*, const char* s, float scale) {
    return s ? static_cast<float>(std::strlen(s)) * 8.0f * scale : 0.0f;
}
static float line_height(void*) { return 14.0f; }
static void noop_clip(void*, float, float, float, float) {}
static void noop_pop(void*) {}

struct EditorCapture {
    bool pattern_data_written = false;
    size_t pattern_data_len = 0;
};

static void capture_set_string(void* opaque, const char* name, const char* value) {
    auto* cap = static_cast<EditorCapture*>(opaque);
    if (std::strcmp(name, "pattern_data") == 0) {
        cap->pattern_data_written = true;
        cap->pattern_data_len = value ? std::strlen(value) : 0;
    }
}

static VividEditorContext make_editor_ctx(EditorCapture& cap,
                                          const char* const* strings,
                                          float* params) {
    VividEditorContext ctx{};
    ctx.surface_width = 1100.0f;
    ctx.surface_height = 640.0f;
    ctx.dpi_scale = 1.0f;
    ctx.draw.draw_rect = noop_rect;
    ctx.draw.draw_rounded_rect = noop_round;
    ctx.draw.draw_text = noop_text;
    ctx.draw.draw_line = noop_line;
    ctx.draw.text_width = text_width;
    ctx.draw.line_height = line_height;
    ctx.draw.push_clip_rect = noop_clip;
    ctx.draw.pop_clip_rect = noop_pop;
    ctx.commands.opaque = &cap;
    ctx.commands.set_string_param = capture_set_string;
    ctx.param_values = params;
    ctx.param_count = 11;
    ctx.output_values = nullptr;
    ctx.output_count = 0;
    ctx.string_param_values = strings;
    ctx.string_param_count = 4;
    return ctx;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path sandbox = fs::path("./.test_midi_clip");
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    const auto midi_path = write_test_midi(sandbox / "clip.mid", 8, 10);
    const auto held_midi_path = write_test_midi(sandbox / "held.mid", 32, 40);
    const auto overlap_midi_path = write_overlap_test_midi(sandbox / "overlap.mid");

    {
        MidiClipCore op;
        op.file.str_value = midi_path.string();
        op.transpose.value = 12.0f;
        op.velocity_scale.value = 0.5f;
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
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
        op.file.str_value = midi_path.string();
        op.loop.value = 1.0f;
        op.main_thread_update(0.0);
        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        int note_on_seen = 0;
        for (uint32_t i = 0; i < notes->count; ++i)
            if (notes->events[i].type == VIVID_NOTE_ON) ++note_on_seen;
        check(note_on_seen >= 2, "MidiClip loops within the same audio block");
    }

    {
        MidiClipCore op;
        op.file.str_value = held_midi_path.string();
        op.main_thread_update(0.0);
        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
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
        auto ctx = make_ctx(16, 1000, custom_outputs);
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
        auto fixture = fs::path(VIVID_SOURCE_DIR) / "assets" / "sweelinck.mid";
        if (fs::exists(fixture)) {
            op.file.str_value = fixture.string();
            op.main_thread_update(0.0);
            float params[11] = {op.length_bars.value, 1.0f, 0, 1, 0, 0, 1, 0, 0, 0, 0};
            const char* strings[4] = {"[]", op.file.str_value.c_str(), "", ""};
            EditorCapture cap;
            auto ectx = make_editor_ctx(cap, strings, params);
            op.draw_editor(&ectx);
            check(!cap.pattern_data_written,
                  "Opening Sweelinck in MidiClip editor does not serialize pattern_data");
        } else {
            std::fprintf(stderr, "SKIP: sweelinck.mid fixture missing\n");
        }
    }

    fs::remove_all(sandbox);
    std::fprintf(stderr, "\n%d failed\n", failures);
    return failures ? 1 : 0;
}
