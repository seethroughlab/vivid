#pragma once

#include "common/midi_file.h"
#include "operator_api/operator.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

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
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_out", VIVID_PORT_OUTPUT, VividMidiBuffer));
        vivid::append_analysis_ports(out);
    }

    void main_thread_update(double /*time*/) override {
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

    void process_audio(const VividAudioContext* ctx) override {
        midi_out_.count = 0;
        if (ctx->custom_outputs && ctx->custom_output_count > 0)
            ctx->custom_outputs[0] = &midi_out_;

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

    VividMidiBuffer midi_out_ = {};
    bool active_notes_[16][128] = {};
    double transport_seconds_ = 0.0;
    size_t next_event_index_ = 0;

    void emit_message(uint8_t status, uint8_t data1, uint8_t data2, uint32_t frame_offset) {
        if (midi_out_.count >= VIVID_MIDI_BUFFER_CAPACITY) return;
        auto& msg = midi_out_.messages[midi_out_.count++];
        msg.status = status;
        msg.data1 = data1;
        msg.data2 = data2;
        msg.reserved = 0;
        msg.frame_offset_samples = frame_offset;

        uint8_t kind = status & 0xF0u;
        uint8_t ch = status & 0x0Fu;
        if (kind == 0x90u && data2 > 0) active_notes_[ch][data1] = true;
        if (kind == 0x80u || (kind == 0x90u && data2 == 0)) active_notes_[ch][data1] = false;
    }

    void stop_all_notes(uint32_t frame_offset) {
        for (uint8_t ch = 0; ch < 16; ++ch) {
            for (uint8_t note = 0; note < 128; ++note) {
                if (!active_notes_[ch][note]) continue;
                emit_message(static_cast<uint8_t>(0x80u | ch), note, 0, frame_offset);
            }
        }
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
