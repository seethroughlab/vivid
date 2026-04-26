#include "operator_api/operator.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "note_helpers.h"
#include "note_id_counter.h"
#include "RtMidi.h"
#include <mutex>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdio>
#include <algorithm>
/**
 * @brief MIDI device listener outputting notes, velocity, gates, CCs, and
 *        per-note expression data with MPE support.
 *
 * Connects to a MIDI input device and outputs note/velocity/gate as both
 * scalar signals (latest note) and polyphonic lane arrays (up to 16 held notes).
 * Also provides pitch bend, mod wheel, a learnable CC value, and per-note
 * expression lanes (pitch_bends, pressures, slides, expressions, channels).
 *
 * Three modes control how expressive data is distributed across lanes:
 * - poly_shared: shared bend/pressure/expression broadcast to all active lanes
 * - mpe_lower: MPE lower zone (manager ch1, members ch2-15)
 * - mpe_upper: MPE upper zone (manager ch16, members ch15-2)
 *
 * @tip Enable learn mode and move a controller to auto-assign the CC number.
 * @param channel MIDI channel filter. 0 = omni (all channels).
 * @param mode Expression routing mode: poly_shared, mpe_lower, or mpe_upper.
 * @see DrumKit, Arpeggiator, Keyboard
 */
struct MidiInput : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "MidiInput";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    // Mode constants
    static constexpr int kModePolyShared = 0;
    static constexpr int kModeMpeLower   = 1;
    static constexpr int kModeMpeUpper   = 2;

    // Params (order matters — indices used for CC learn write-back)
    vivid::Param<int>  device   {"device",    0, 0, 15};    // [0]
    vivid::Param<int>  channel  {"channel",   0, 0, 16};    // [1] 0 = omni
    vivid::Param<int>  cc_number{"cc_number", 1, 0, 127};   // [2]
    vivid::Param<bool> learn    {"learn",     false};        // [3]
    vivid::Param<int>  mode     {"mode",      0, {"poly_shared", "mpe_lower", "mpe_upper"}};  // [4]

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&device);
        out.push_back(&channel);
        out.push_back(&cc_number);
        out.push_back(&learn);
        out.push_back(&mode);
    }

    // Output ports
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"note",        VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [0]
        out.push_back({"velocity",    VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [1]
        out.push_back({"gate",        VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [2]
        out.push_back({"trigger",     VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [3]
        out.push_back({"pitch_bend",  VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [4]
        out.push_back({"mod_wheel",   VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [5]
        out.push_back({"cc_value",    VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [6]
        out.push_back({"notes",       VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [7]
        out.push_back({"velocities",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [8]
        out.push_back({"gates",       VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [9]
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer));  // [10]
        // New scalar outputs
        out.push_back({"aftertouch",  VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [11]
        out.push_back({"expression",  VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT});  // [12]
        // New lane-array outputs
        out.push_back({"lane_ids",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [13]
        out.push_back({"pitch_bends", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [14]
        out.push_back({"pressures",   VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [15]
        out.push_back({"slides",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [16]
        out.push_back({"expressions", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [17]
        out.push_back({"channels",    VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});  // [18]
    }

    MidiInput() {
        std::memset(cc_values_, 0, sizeof(cc_values_));

        vivid::description(device, "MIDI input device index");
        vivid::description(channel, "MIDI channel filter, 0 = omni (all channels)");
        vivid::description(cc_number, "CC number to read (0-127)");
        vivid::description(learn, "When enabled, auto-assigns cc_number from the next incoming CC");
        vivid::description(mode, "Expression routing: poly_shared, mpe_lower, or mpe_upper");

        vivid::semantic_tag(channel, "index");
        vivid::semantic_shape(channel, "int");

        vivid::semantic_tag(cc_number, "index");
        vivid::semantic_shape(cc_number, "int");

        vivid::semantic_tag(learn, "enabled");
        vivid::semantic_shape(learn, "bool");

        vivid::semantic_tag(mode, "mode");
        vivid::semantic_shape(mode, "int");
    }

    // Test seam: skip RtMidi initialization for deterministic unit testing.
    void skip_midi_init() { midi_init_attempted_ = true; }

    // Test seam: push raw MIDI messages into the event buffer without real hardware.
    void inject_events(const std::vector<std::vector<unsigned char>>& messages) {
        std::lock_guard<std::mutex> lock(event_mutex_);
        for (const auto& m : messages)
            event_buffer_.push_back(m);
    }

    void process_frame(const VividFrameContext* ctx) override {
        ensure_midi_initialized();
        int desired_device = device.int_value();

        // Open/reopen port if device param changed
        if (midi_in_ && desired_device != current_device_) {
            if (current_device_ >= 0) {
                midi_in_->closePort();
            }
            unsigned int port_count = midi_in_->getPortCount();
            if (desired_device >= 0 && static_cast<unsigned>(desired_device) < port_count) {
                try {
                    midi_in_->openPort(static_cast<unsigned>(desired_device));
                    fprintf(stderr, "[MidiInput] Opened port %d: %s\n",
                            desired_device,
                            midi_in_->getPortName(static_cast<unsigned>(desired_device)).c_str());
                } catch (RtMidiError& e) {
                    fprintf(stderr, "[MidiInput] Error opening port %d: %s\n",
                            desired_device, e.getMessage().c_str());
                }
            }
            current_device_ = desired_device;
        }

        // Drain event buffer
        std::vector<std::vector<unsigned char>> events;
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            events.swap(event_buffer_);
        }

        int chan_filter = channel.int_value();  // 0 = omni
        int cur_mode = mode.int_value();
        bool had_note_on = false;

        // Reset the outbound note buffer for this frame; held_note_on/off
        // and per-note expression dispatchers below append to it.
        notes_out_buf_.count = 0;

        for (const auto& msg : events) {
            if (msg.size() < 1) continue;

            unsigned char status = msg[0];
            unsigned char msg_type = status & 0xF0;
            uint8_t msg_chan = (status & 0x0F) + 1;  // 1-based channel

            // Channel filter (0 = omni = accept all)
            if (chan_filter != 0 && msg_chan != chan_filter) continue;

            if (msg_type == 0x90 && msg.size() >= 3) {
                // Note On
                unsigned char note = msg[1];
                unsigned char vel  = msg[2];
                if (vel > 0) {
                    last_note_ = note;
                    last_velocity_ = static_cast<float>(vel) / 127.0f;
                    had_note_on = true;
                    held_note_on(cur_mode, msg_chan, note, static_cast<float>(vel) / 127.0f);
                    fprintf(stderr, "[MidiInput] Note ON: %d vel=%d ch=%d\n", note, vel, msg_chan);
                } else {
                    // Note On with vel=0 is Note Off
                    held_note_off(cur_mode, msg_chan, note);
                    fprintf(stderr, "[MidiInput] Note OFF: %d (vel=0) ch=%d\n", note, msg_chan);
                }
            } else if (msg_type == 0x80 && msg.size() >= 3) {
                // Note Off
                unsigned char note = msg[1];
                held_note_off(cur_mode, msg_chan, note);
                fprintf(stderr, "[MidiInput] Note OFF: %d ch=%d\n", note, msg_chan);
            } else if (msg_type == 0xB0 && msg.size() >= 3) {
                // Control Change
                unsigned char cc  = msg[1];
                unsigned char val = msg[2];
                cc_values_[cc] = static_cast<float>(val) / 127.0f;

                // CC Learn
                if (learn.bool_value()) {
                    ctx->param_values[2] = static_cast<float>(cc);  // cc_number
                    ctx->param_values[3] = 0.0f;                    // learn = false
                    fprintf(stderr, "[MidiInput] CC Learn: captured CC %d\n", cc);
                }

                // CC74 (slide → MPE timbre) and CC11 (expression).
                // In MPE, both are per-channel and emit native per-note events
                // for slide. CC11 has no native event type and stays as a
                // lane-array output only.
                if (cc == 74) {
                    float v = static_cast<float>(val) / 127.0f;
                    slide_scalar_ = v;
                    if (is_mpe_mode(cur_mode) && is_member_channel(cur_mode, msg_chan)) {
                        if (auto* h = find_held_by_channel(msg_chan)) {
                            h->slide = v;
                            vivid_sequencers::note_timbre(notes_out_buf_, h->note_id, v);
                        }
                    } else if (cur_mode == kModePolyShared) {
                        for (int i = 0; i < held_count_; ++i)
                            held_buffer_[i].slide = v;
                    }
                }
                if (cc == 11) {
                    float v = static_cast<float>(val) / 127.0f;
                    expression_scalar_ = v;
                    if (is_mpe_mode(cur_mode) && is_member_channel(cur_mode, msg_chan)) {
                        if (auto* h = find_held_by_channel(msg_chan)) h->expression = v;
                    } else if (cur_mode == kModePolyShared) {
                        for (int i = 0; i < held_count_; ++i)
                            held_buffer_[i].expression = v;
                    }
                }
            } else if (msg_type == 0xE0 && msg.size() >= 3) {
                // Pitch Bend — MPE wire range is ±48 semitones (default ±2;
                // we send the normalized -1..1 here and let synths scale).
                int bend_raw = (static_cast<int>(msg[2]) << 7) | static_cast<int>(msg[1]);
                float bend = static_cast<float>(bend_raw - 8192) / 8192.0f;
                pitch_bend_ = bend;
                if (is_mpe_mode(cur_mode) && is_member_channel(cur_mode, msg_chan)) {
                    if (auto* h = find_held_by_channel(msg_chan)) {
                        h->pitch_bend = bend;
                        // Emit as semitones (MPE default ±2 semitones).
                        vivid_sequencers::note_pitch_bend(
                            notes_out_buf_, h->note_id, bend * 2.0f);
                    }
                } else if (cur_mode == kModePolyShared) {
                    for (int i = 0; i < held_count_; ++i)
                        held_buffer_[i].pitch_bend = bend;
                }
            } else if (msg_type == 0xD0 && msg.size() >= 2) {
                // Channel Pressure (Aftertouch) — 2-byte message
                float pressure = static_cast<float>(msg[1]) / 127.0f;
                aftertouch_ = pressure;
                if (is_mpe_mode(cur_mode) && is_member_channel(cur_mode, msg_chan)) {
                    if (auto* h = find_held_by_channel(msg_chan)) {
                        h->pressure = pressure;
                        vivid_sequencers::note_pressure(
                            notes_out_buf_, h->note_id, pressure);
                    }
                } else if (cur_mode == kModePolyShared) {
                    for (int i = 0; i < held_count_; ++i)
                        held_buffer_[i].pressure = pressure;
                }
            } else if (msg_type == 0xA0 && msg.size() >= 3) {
                // Polyphonic Key Pressure — true per-note, emit native event
                // regardless of mode.
                unsigned char note = msg[1];
                float pressure = static_cast<float>(msg[2]) / 127.0f;
                if (is_mpe_mode(cur_mode)) {
                    for (int i = 0; i < held_count_; ++i) {
                        if (held_buffer_[i].channel == msg_chan && held_buffer_[i].note == note) {
                            held_buffer_[i].pressure = pressure;
                            vivid_sequencers::note_pressure(
                                notes_out_buf_, held_buffer_[i].note_id, pressure);
                            break;
                        }
                    }
                } else {
                    for (int i = 0; i < held_count_; ++i) {
                        if (held_buffer_[i].note == note) {
                            held_buffer_[i].pressure = pressure;
                            vivid_sequencers::note_pressure(
                                notes_out_buf_, held_buffer_[i].note_id, pressure);
                        }
                    }
                }
            }
        }

        // Publish the native note buffer assembled by the dispatchers above.
        if (ctx->custom_outputs && ctx->custom_output_count > 0) {
            ctx->custom_outputs[0] = &notes_out_buf_;
        }

        // Write scalar outputs
        int cc_idx = cc_number.int_value();
        if (cc_idx < 0) cc_idx = 0;
        if (cc_idx > 127) cc_idx = 127;

        ctx->output_values[0]  = static_cast<float>(last_note_);        // note
        ctx->output_values[1]  = last_velocity_;                         // velocity
        ctx->output_values[2]  = (held_count_ > 0) ? 1.0f : 0.0f;      // gate
        ctx->output_values[3]  = had_note_on ? 1.0f : 0.0f;             // trigger
        ctx->output_values[4]  = pitch_bend_;                            // pitch_bend
        ctx->output_values[5]  = cc_values_[1];                          // mod_wheel (CC1)
        ctx->output_values[6]  = cc_values_[cc_idx];                     // cc_value
        ctx->output_values[11] = aftertouch_;                            // aftertouch
        ctx->output_values[12] = expression_scalar_;                     // expression

        // Write lane outputs: all currently held notes
        if (ctx->output_lanes) {
            uint32_t len = static_cast<uint32_t>(held_count_);

            // Original lane arrays [7-9]
            auto& notes_lane    = ctx->output_lanes[7];
            auto& velocity_lane = ctx->output_lanes[8];
            auto& gates_lane    = ctx->output_lanes[9];

            // New lane arrays [13-18]
            auto& lane_ids_lane    = ctx->output_lanes[13];
            auto& pitch_bends_lane = ctx->output_lanes[14];
            auto& pressures_lane   = ctx->output_lanes[15];
            auto& slides_lane      = ctx->output_lanes[16];
            auto& expressions_lane = ctx->output_lanes[17];
            auto& channels_lane    = ctx->output_lanes[18];

            float* notes_buf       = notes_lane.resize(notes_lane.handle, len);
            float* vel_buf         = velocity_lane.resize(velocity_lane.handle, len);
            float* gates_buf       = gates_lane.resize(gates_lane.handle, len);
            float* lane_ids_buf    = lane_ids_lane.resize(lane_ids_lane.handle, len);
            float* bends_buf       = pitch_bends_lane.resize(pitch_bends_lane.handle, len);
            float* press_buf       = pressures_lane.resize(pressures_lane.handle, len);
            float* slides_buf      = slides_lane.resize(slides_lane.handle, len);
            float* expr_buf        = expressions_lane.resize(expressions_lane.handle, len);
            float* chan_buf         = channels_lane.resize(channels_lane.handle, len);

            if (notes_buf && vel_buf && gates_buf && lane_ids_buf &&
                bends_buf && press_buf && slides_buf && expr_buf && chan_buf) {
                for (uint32_t i = 0; i < len; ++i) {
                    const auto& h = held_buffer_[i];
                    notes_buf[i]    = static_cast<float>(h.note);
                    vel_buf[i]      = h.velocity;
                    gates_buf[i]    = 1.0f;
                    lane_ids_buf[i] = static_cast<float>(h.note_id);
                    bends_buf[i]    = h.pitch_bend;
                    press_buf[i]    = h.pressure;
                    slides_buf[i]   = h.slide;
                    expr_buf[i]     = h.expression;
                    chan_buf[i]     = static_cast<float>(h.channel);
                }
                notes_lane.commit(notes_lane.handle, len);
                velocity_lane.commit(velocity_lane.handle, len);
                gates_lane.commit(gates_lane.handle, len);
                lane_ids_lane.commit(lane_ids_lane.handle, len);
                pitch_bends_lane.commit(pitch_bends_lane.handle, len);
                pressures_lane.commit(pressures_lane.handle, len);
                slides_lane.commit(slides_lane.handle, len);
                expressions_lane.commit(expressions_lane.handle, len);
                channels_lane.commit(channels_lane.handle, len);
            }
        }
    }

private:
    static constexpr int kMaxHeld = 16;

    struct HeldNote {
        uint8_t  note       = 0;
        float    velocity   = 0.0f;
        uint8_t  channel    = 0;      // 1-based MIDI channel
        uint64_t note_id    = 0;      // stable per-note identity
        float    pitch_bend = 0.0f;   // per-note bend (-1..1)
        float    pressure   = 0.0f;   // per-note pressure (0..1)
        float    slide      = 0.0f;   // per-note slide/CC74 (0..1)
        float    expression = 0.0f;   // per-note expression/CC11 (0..1)
    };

    std::unique_ptr<RtMidiIn> midi_in_;
    bool midi_init_attempted_ = false;
    std::mutex event_mutex_;
    std::vector<std::vector<unsigned char>> event_buffer_;
    float cc_values_[128];
    int current_device_ = -1;
    int last_note_ = 0;
    float last_velocity_ = 0.0f;
    float pitch_bend_ = 0.0f;
    float aftertouch_ = 0.0f;
    float expression_scalar_ = 0.0f;
    float slide_scalar_ = 0.0f;

    HeldNote held_buffer_[kMaxHeld] = {};
    int held_count_ = 0;
    VividNoteBuffer notes_out_buf_ = {};

    // --- MPE helpers ---

    static bool is_mpe_mode(int m) { return m == kModeMpeLower || m == kModeMpeUpper; }

    static bool is_member_channel(int m, uint8_t ch) {
        if (m == kModeMpeLower) return ch >= 2 && ch <= 15;
        if (m == kModeMpeUpper) return ch >= 2 && ch <= 15;
        return false;
    }

    static bool is_manager_channel(int m, uint8_t ch) {
        if (m == kModeMpeLower) return ch == 1;
        if (m == kModeMpeUpper) return ch == 16;
        return false;
    }

    // Find the held note on a specific MIDI channel (MPE: one note per member channel).
    HeldNote* find_held_by_channel(uint8_t ch) {
        for (int i = 0; i < held_count_; ++i) {
            if (held_buffer_[i].channel == ch) return &held_buffer_[i];
        }
        return nullptr;
    }

    // --- Held note management ---

    void held_note_on(int cur_mode, uint8_t ch, uint8_t note, float velocity) {
        if (is_mpe_mode(cur_mode)) {
            // MPE: match by (channel, note)
            for (int i = 0; i < held_count_; ++i) {
                if (held_buffer_[i].channel == ch && held_buffer_[i].note == note) {
                    held_buffer_[i].velocity = velocity;
                    return;
                }
            }
        } else {
            // poly_shared: match by note only (original behavior)
            for (int i = 0; i < held_count_; ++i) {
                if (held_buffer_[i].note == note) {
                    held_buffer_[i].velocity = velocity;
                    return;
                }
            }
        }
        // Add new held note + emit native NOTE_ON
        if (held_count_ < kMaxHeld) {
            auto& h = held_buffer_[held_count_];
            h.note       = note;
            h.velocity   = velocity;
            h.channel    = ch;
            h.note_id    = vivid_sequencers::next_note_id();
            h.pitch_bend = 0.0f;
            h.pressure   = 0.0f;
            h.slide      = (cur_mode == kModePolyShared) ? slide_scalar_ : 0.0f;
            h.expression = (cur_mode == kModePolyShared) ? expression_scalar_ : 0.0f;
            held_count_++;
            vivid_sequencers::note_on(notes_out_buf_, note, velocity, h.note_id);
        }
    }

    void held_note_off(int cur_mode, uint8_t ch, uint8_t note) {
        for (int i = 0; i < held_count_; ++i) {
            bool match;
            if (is_mpe_mode(cur_mode)) {
                match = (held_buffer_[i].channel == ch && held_buffer_[i].note == note);
            } else {
                match = (held_buffer_[i].note == note);
            }
            if (match) {
                vivid_sequencers::note_off(notes_out_buf_, held_buffer_[i].note_id);
                // Shift remaining down
                for (int j = i; j < held_count_ - 1; ++j) {
                    held_buffer_[j] = held_buffer_[j + 1];
                }
                held_count_--;
                return;
            }
        }
    }

    void ensure_midi_initialized() {
        if (midi_init_attempted_) return;
        midi_init_attempted_ = true;

        try {
            midi_in_ = std::make_unique<RtMidiIn>();

            // Log available ports once on first real processing pass.
            unsigned int port_count = midi_in_->getPortCount();
            fprintf(stderr, "[MidiInput] Available MIDI ports (%u):\n", port_count);
            for (unsigned int i = 0; i < port_count; i++) {
                fprintf(stderr, "  [%u] %s\n", i, midi_in_->getPortName(i).c_str());
            }

            midi_in_->setCallback(&MidiInput::midi_callback, this);
            midi_in_->ignoreTypes(true, true, true);  // ignore sysex, timing, active sensing
        } catch (RtMidiError& e) {
            fprintf(stderr, "[MidiInput] RtMidi init error: %s\n", e.getMessage().c_str());
            midi_in_.reset();
        } catch (...) {
            fprintf(stderr, "[MidiInput] Unknown MIDI init error\n");
            midi_in_.reset();
        }
    }

    static void midi_callback(double /*timestamp*/,
                               std::vector<unsigned char>* message,
                               void* user_data) {
        auto* self = static_cast<MidiInput*>(user_data);
        if (message && !message->empty()) {
            std::lock_guard<std::mutex> lock(self->event_mutex_);
            self->event_buffer_.push_back(*message);
        }
    }
};

VIVID_REGISTER(MidiInput)
