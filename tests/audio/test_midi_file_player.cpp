#include "audio/midi_file_player/midi_file_player.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

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
    track.insert(track.end(), {0xFF, 0x51, 0x03, 0x01, 0x86, 0xA0}); // 100000 us/qn
    push_varlen(track, 0);
    track.insert(track.end(), {0x90, 60, 100});
    push_varlen(track, note_off_ticks);
    track.insert(track.end(), {0x80, 60, 0});
    push_varlen(track, duration_ticks > note_off_ticks ? duration_ticks - note_off_ticks : 0);
    track.insert(track.end(), {0xFF, 0x51, 0x03, 0x01, 0x86, 0xA0});
    push_varlen(track, 0);
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
    ctx.custom_outputs = custom_outputs;
    ctx.custom_output_count = 1;
    return ctx;
}

int main() {
    namespace fs = std::filesystem;
    const fs::path sandbox = fs::path("./.test_midi_file_player");
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);
    const auto midi_path = write_test_midi(sandbox / "player.mid", 8, 10);
    const auto held_midi_path = write_test_midi(sandbox / "held.mid", 32, 40);

    {
        MidiFilePlayer op;
        op.file.str_value = midi_path.string();
        op.transpose.value = 12.0f;
        op.velocity_scale.value = 0.5f;
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);

        check(custom_outputs[0] != nullptr, "midi_out custom output is published");
        auto* midi = static_cast<VividMidiBuffer*>(custom_outputs[0]);
        check(midi->count >= 2, "first buffer emits note-on and note-off");
        if (midi->count >= 2) {
            check(midi->messages[0].status == 0x90, "first emitted message is note-on");
            check(midi->messages[0].data1 == 72, "transpose shifts emitted note");
            check(midi->messages[0].data2 == 50, "velocity scale adjusts note-on velocity");
            check(midi->messages[0].frame_offset_samples == 0, "note-on lands at frame 0");
            check(midi->messages[1].status == 0x80, "second emitted message is note-off");
            check(midi->messages[1].frame_offset_samples == 8, "note-off lands at parsed frame offset");
        }
    }

    {
        MidiFilePlayer op;
        op.file.str_value = midi_path.string();
        op.loop.value = 1.0f;
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);

        auto* midi = static_cast<VividMidiBuffer*>(custom_outputs[0]);
        bool saw_second_loop_note_on = false;
        for (uint32_t i = 0; i < midi->count; ++i) {
            if (midi->messages[i].status == 0x90 && midi->messages[i].frame_offset_samples >= 9) {
                saw_second_loop_note_on = true;
                break;
            }
        }
        check(saw_second_loop_note_on, "looping restarts playback within the same buffer when needed");
    }

    {
        MidiFilePlayer op;
        op.file.str_value = held_midi_path.string();
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);
        op.playing.value = 0.0f;
        op.process_audio(&ctx);

        auto* midi = static_cast<VividMidiBuffer*>(custom_outputs[0]);
        bool saw_note_off = false;
        for (uint32_t i = 0; i < midi->count; ++i) {
            if ((midi->messages[i].status & 0xF0u) == 0x80u) {
                saw_note_off = true;
                break;
            }
        }
        check(saw_note_off, "stopping playback flushes active notes");
    }

    fs::remove_all(sandbox);
    std::fprintf(stderr, "\n%d failed\n", failures);
    return failures ? 1 : 0;
}
