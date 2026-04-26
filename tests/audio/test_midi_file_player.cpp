#include "audio/midi_file_player/midi_file_player.h"

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
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

static std::filesystem::path write_overlap_test_midi(const std::filesystem::path& path) {
    std::vector<uint8_t> track;
    push_varlen(track, 0);
    track.insert(track.end(), {0xFF, 0x51, 0x03, 0x01, 0x86, 0xA0}); // 100000 us/qn
    push_varlen(track, 0);
    track.insert(track.end(), {0x90, 60, 100});   // on #1 at tick 0
    push_varlen(track, 2);
    track.insert(track.end(), {0x90, 60, 80});    // on #2 at tick 2
    push_varlen(track, 2);
    track.insert(track.end(), {0x80, 60, 0});     // off #1 at tick 4
    push_varlen(track, 2);
    track.insert(track.end(), {0x80, 60, 0});     // off #2 at tick 6
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
    const auto overlap_midi_path = write_overlap_test_midi(sandbox / "overlap.mid");

    {
        MidiFilePlayer op;
        op.file.str_value = midi_path.string();
        op.transpose.value = 12.0f;
        op.velocity_scale.value = 0.5f;
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);

        check(custom_outputs[0] != nullptr, "notes_out custom output is published");
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        check(notes->count >= 2, "first buffer emits NOTE_ON and NOTE_OFF");
        if (notes->count >= 2) {
            const auto& on  = notes->events[0];
            const auto& off = notes->events[1];
            check(on.type == VIVID_NOTE_ON, "first emitted event is NOTE_ON");
            check(on.note_number == 72, "transpose shifts emitted note (60 + 12 = 72)");
            check_float(on.value, 50.0f / 127.0f, 1e-4f,
                        "velocity scale (100*0.5 = 50/127) reflected on NOTE_ON");
            check(on.frame_offset_samples == 0, "NOTE_ON lands at frame 0");
            check(on.note_id != 0, "NOTE_ON carries a non-zero note_id");
            check(off.type == VIVID_NOTE_OFF, "second emitted event is NOTE_OFF");
            check(off.note_id == on.note_id,
                  "NOTE_OFF carries the same note_id as the matching NOTE_ON");
            check(off.frame_offset_samples == 8, "NOTE_OFF lands at parsed frame offset");
        }
    }

    {
        MidiFilePlayer op;
        op.file.str_value = midi_path.string();
        op.prepare_instance_assets();

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);

        check(custom_outputs[0] != nullptr,
              "warmup path publishes notes output without main_thread_update");
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        check(notes->count >= 2,
              "prepare_instance_assets preloads sequence before audio starts");
    }

    {
        MidiFilePlayer op;
        op.file.str_value = midi_path.string();
        op.loop.value = 1.0f;
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);

        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        bool saw_second_loop_note_on = false;
        uint64_t first_id = 0;
        for (uint32_t i = 0; i < notes->count; ++i) {
            const auto& e = notes->events[i];
            if (e.type == VIVID_NOTE_ON) {
                if (first_id == 0) {
                    first_id = e.note_id;
                } else if (e.frame_offset_samples >= 9 && e.note_id != first_id) {
                    saw_second_loop_note_on = true;
                    break;
                }
            }
        }
        check(saw_second_loop_note_on,
              "looping restarts playback within the same buffer with a fresh note_id");
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

        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        bool saw_note_off = false;
        for (uint32_t i = 0; i < notes->count; ++i) {
            if (notes->events[i].type == VIVID_NOTE_OFF) {
                saw_note_off = true;
                break;
            }
        }
        check(saw_note_off, "stopping playback flushes active notes (NOTE_OFFs emitted)");
    }

    {
        MidiFilePlayer op;
        op.file.str_value = overlap_midi_path.string();
        op.main_thread_update(0.0);

        void* custom_outputs[1] = {nullptr};
        auto ctx = make_ctx(16, 1000, custom_outputs);
        op.process_audio(&ctx);

        check(custom_outputs[0] != nullptr, "overlap fixture publishes notes_out");
        auto* notes = static_cast<VividNoteBuffer*>(custom_outputs[0]);
        check(notes->count >= 4, "overlap fixture emits two NOTE_ONs and two NOTE_OFFs");

        uint64_t first_on_id = 0;
        uint64_t second_on_id = 0;
        uint64_t first_off_id = 0;
        uint64_t second_off_id = 0;
        int note_on_seen = 0;
        int note_off_seen = 0;
        for (uint32_t i = 0; i < notes->count; ++i) {
            const auto& e = notes->events[i];
            if (e.type == VIVID_NOTE_ON) {
                if (note_on_seen == 0) first_on_id = e.note_id;
                if (note_on_seen == 1) second_on_id = e.note_id;
                ++note_on_seen;
            } else if (e.type == VIVID_NOTE_OFF) {
                if (note_off_seen == 0) first_off_id = e.note_id;
                if (note_off_seen == 1) second_off_id = e.note_id;
                ++note_off_seen;
            }
        }

        check(note_on_seen == 2, "exactly two NOTE_ON events seen");
        check(note_off_seen == 2, "exactly two NOTE_OFF events seen");
        check(first_on_id != 0 && second_on_id != 0 && first_on_id != second_on_id,
              "overlapping NOTE_ONs allocate distinct ids");
        check(first_off_id == first_on_id, "first NOTE_OFF releases the oldest matching note");
        check(second_off_id == second_on_id, "second NOTE_OFF releases the newer matching note");
    }

    fs::remove_all(sandbox);
    std::fprintf(stderr, "\n%d failed\n", failures);
    return failures ? 1 : 0;
}
