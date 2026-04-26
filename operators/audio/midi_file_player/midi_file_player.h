#pragma once

#include "common/midi_file.h"
#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "note_helpers.h"
#include "note_id_counter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief Plays standard MIDI files and outputs note, velocity, and gate data.
 *
 * Loads and plays .mid files, outputting real-time note events as control
 * signals and polyphonic lane arrays. Supports loop, once, and hold-last
 * play modes.
 *
 * @see MidiInput, Sampler, Sequencer
 */
struct MidiFilePlayer : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "MidiFilePlayer";
    static constexpr bool kTimeDependent = true;

    vivid::Param<vivid::FilePath> file {"file"};
    vivid::Param<bool> playing {"playing", true};
    vivid::Param<bool> loop {"loop", false};
    vivid::Param<int> transpose {"transpose", 0, -48, 48};
    vivid::Param<float> velocity_scale {"velocity_scale", 1.0f, 0.0f, 4.0f};

    MidiFilePlayer() {
        vivid::semantic_shape(file, "path");
        vivid::description(file, "Path to a .mid MIDI file to play");
        vivid::description(playing, "Enable or disable playback");
        vivid::description(loop, "Loop the MIDI file when it reaches the end");
        vivid::description(transpose, "Shift all notes up or down in semitones (-48 to +48)");
        vivid::description(velocity_scale, "Scale note velocities (1 = original, 0 = silent)");
    }

    ~MidiFilePlayer() override {
        delete sequence_.load(std::memory_order_relaxed);
        delete deferred_delete_;
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&playing);
        out.push_back(&loop);
        out.push_back(&transpose);
        out.push_back(&velocity_scale);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));
        vivid::append_analysis_ports(out);
    }

    void prepare_instance_assets() override {
        refresh_sequence();
    }

    void main_thread_update(double /*time*/) override {
        refresh_sequence();
    }

    void process_audio(const VividAudioContext* ctx) override {
        notes_out_.count = 0;
        if (ctx->custom_outputs && ctx->custom_output_count > 0)
            ctx->custom_outputs[0] = &notes_out_;

        SequenceData* seq = sequence_.load(std::memory_order_acquire);
        const uint64_t generation = sequence_generation_.load(std::memory_order_acquire);
        if (generation != audio_generation_) {
            audio_generation_ = generation;
            transport_seconds_ = 0.0;
            next_event_index_ = 0;
            stop_all_notes(0);
        }

        if (!seq || seq->sequence.events.empty() || seq->sequence.duration_seconds <= 0.0) {
            return;
        }

        if (!playing.bool_value()) {
            stop_all_notes(0);
            return;
        }

        const double sample_rate = std::max(1u, ctx->sample_rate);
        uint32_t frame_cursor = 0;
        while (frame_cursor < ctx->buffer_size) {
            const double remaining_seconds =
                static_cast<double>(ctx->buffer_size - frame_cursor) / sample_rate;
            double segment_end = transport_seconds_ + remaining_seconds;
            bool wraps = false;
            uint32_t segment_frames = ctx->buffer_size - frame_cursor;

            if (loop.bool_value() && seq->sequence.duration_seconds > 0.0 &&
                segment_end >= seq->sequence.duration_seconds) {
                wraps = true;
                double to_boundary = std::max(0.0, seq->sequence.duration_seconds - transport_seconds_);
                segment_frames = static_cast<uint32_t>(std::min<double>(
                    ctx->buffer_size - frame_cursor,
                    std::max(0.0, std::round(to_boundary * sample_rate))));
                if (segment_frames == 0 && ctx->buffer_size > frame_cursor)
                    segment_frames = 1;
                segment_end = transport_seconds_ + static_cast<double>(segment_frames) / sample_rate;
            }

            emit_events_in_range(seq->sequence, transport_seconds_, segment_end, frame_cursor,
                                 ctx->buffer_size, ctx->sample_rate, transpose.int_value(),
                                 velocity_scale.value);

            transport_seconds_ = segment_end;
            frame_cursor += segment_frames;

            if (wraps) {
                stop_all_notes(frame_cursor == 0 ? 0 : std::min(frame_cursor - 1, ctx->buffer_size - 1));
                transport_seconds_ = 0.0;
                next_event_index_ = 0;
                continue;
            }

            if (transport_seconds_ >= seq->sequence.duration_seconds) {
                stop_all_notes(frame_cursor == 0 ? 0 : std::min(frame_cursor - 1, ctx->buffer_size - 1));
                transport_seconds_ = seq->sequence.duration_seconds;
                break;
            }
        }
    }

private:
    struct SequenceData {
        vivid::midi_file::Sequence sequence;
    };

    std::atomic<SequenceData*> sequence_{nullptr};
    SequenceData* deferred_delete_ = nullptr;
    std::atomic<uint64_t> sequence_generation_{1};
    uint64_t audio_generation_ = 0;
    std::string last_path_;

    VividNoteBuffer notes_out_ = {};
    // Per active note: (channel, note_number, note_id). Stored as a flat
    // list because SMF can have overlapping note-ons on the same
    // (channel, note) pair; each one needs its own id to release independently.
    static constexpr int kMaxActive = 64;
    struct ActiveNote { uint8_t channel; uint8_t note; uint64_t note_id; };
    ActiveNote active_notes_[kMaxActive] = {};
    int active_count_ = 0;
    double transport_seconds_ = 0.0;
    size_t next_event_index_ = 0;

    void refresh_sequence() {
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        const std::string& path = file.str_value;
        if (path == last_path_) return;
        last_path_ = path;

        SequenceData* new_seq = nullptr;
        if (!path.empty()) {
            auto parsed = vivid::midi_file::parse_file(path);
            if (!parsed.ok()) {
                std::fprintf(stderr, "[midi_file_player] Failed to parse %s: %s\n",
                             path.c_str(), parsed.error.c_str());
            } else {
                new_seq = new SequenceData();
                new_seq->sequence = std::move(parsed);
            }
        }

        SequenceData* old = sequence_.exchange(new_seq, std::memory_order_acq_rel);
        deferred_delete_ = old;
        sequence_generation_.fetch_add(1, std::memory_order_acq_rel);
    }

    // Translate a raw SMF event into the native note transport. Allocates
    // a fresh note_id per NOTE_ON; matches NOTE_OFF to the FIFO-first
    // matching active id so overlapping notes release in order. Non-note
    // SMF events (CC, channel pressure, channel pitch_bend) are dropped —
    // Phase 1 carries only per-note data on the wire.
    void emit_message(uint8_t status, uint8_t data1, uint8_t data2, uint32_t frame_offset) {
        uint8_t kind = status & 0xF0u;
        uint8_t ch = status & 0x0Fu;
        bool is_note_on  = (kind == 0x90u) && (data2 > 0);
        bool is_note_off = (kind == 0x80u) || ((kind == 0x90u) && (data2 == 0));

        if (is_note_on) {
            if (active_count_ >= kMaxActive) return;
            uint64_t id = vivid_sequencers::next_note_id();
            active_notes_[active_count_++] = {ch, data1, id};
            vivid_sequencers::note_on(notes_out_, data1,
                                      static_cast<float>(data2) / 127.0f,
                                      id, frame_offset);
        } else if (is_note_off) {
            for (int i = 0; i < active_count_; ++i) {
                if (active_notes_[i].channel == ch && active_notes_[i].note == data1) {
                    vivid_sequencers::note_off(notes_out_,
                                               active_notes_[i].note_id,
                                               frame_offset);
                    for (int j = i; j < active_count_ - 1; ++j)
                        active_notes_[j] = active_notes_[j + 1];
                    --active_count_;
                    return;
                }
            }
        }
    }

    void stop_all_notes(uint32_t frame_offset) {
        for (int i = 0; i < active_count_; ++i) {
            vivid_sequencers::note_off(notes_out_,
                                       active_notes_[i].note_id,
                                       frame_offset);
        }
        active_count_ = 0;
    }

    static uint8_t clamp_note(int note) {
        return static_cast<uint8_t>(std::clamp(note, 0, 127));
    }

    static uint8_t clamp_velocity(float vel) {
        return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(vel), 0, 127));
    }

    void emit_events_in_range(const vivid::midi_file::Sequence& sequence,
                              double start_time,
                              double end_time,
                              uint32_t frame_base,
                              uint32_t buffer_size,
                              uint32_t sample_rate,
                              int transpose_semitones,
                              float velocity_mul) {
        while (next_event_index_ < sequence.events.size() &&
               sequence.events[next_event_index_].time_seconds < start_time) {
            ++next_event_index_;
        }

        while (next_event_index_ < sequence.events.size()) {
            const auto& ev = sequence.events[next_event_index_];
            if (ev.time_seconds >= end_time) break;

            double rel_seconds = std::max(0.0, ev.time_seconds - start_time);
            uint32_t frame_offset = frame_base + static_cast<uint32_t>(
                std::floor(rel_seconds * static_cast<double>(std::max(1u, sample_rate))));
            frame_offset = std::min(frame_offset, buffer_size > 0 ? buffer_size - 1 : 0);

            uint8_t status = ev.status;
            uint8_t data1 = ev.data1;
            uint8_t data2 = ev.data2;
            uint8_t kind = status & 0xF0u;

            if (kind == 0x80u || kind == 0x90u || kind == 0xA0u) {
                data1 = clamp_note(static_cast<int>(data1) + transpose_semitones);
            }
            if (kind == 0x90u) {
                data2 = clamp_velocity(static_cast<float>(data2) * velocity_mul);
            }

            emit_message(status, data1, data2, frame_offset);
            ++next_event_index_;
        }
    }
};
