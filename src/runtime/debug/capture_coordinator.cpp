#include "runtime/debug/capture_coordinator.h"
#include "runtime/platform/av_exporter.h"
#include "runtime/audio/audio_engine.h"
#include "runtime/control/runtime_api.h"
#include "runtime/gpu/gpu_context.h"
#include "ui/graph/node_graph.h"
#include "common/gpu_util.h"
#include <webgpu/wgpu.h>
#include <stb_image_write.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <fstream>

namespace vivid {

CaptureCoordinator::CaptureCoordinator() : exporter_(std::make_unique<AVExporter>()) {}
CaptureCoordinator::CaptureCoordinator(std::unique_ptr<AVExporter> exporter)
    : exporter_(std::move(exporter)) {}
CaptureCoordinator::~CaptureCoordinator() = default;

// ---------------------------------------------------------------------------
// Base64 encoder (RFC 4648)
// ---------------------------------------------------------------------------

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kBase64Table[(n >> 18) & 0x3F]);
        out.push_back(kBase64Table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kBase64Table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kBase64Table[n & 0x3F] : '=');
    }
    return out;
}

// ---------------------------------------------------------------------------
// WAV encoder (44-byte header + float32 PCM)
// ---------------------------------------------------------------------------

static std::vector<uint8_t> encode_wav_float32(const float* samples, uint64_t sample_count,
                                                uint32_t sample_rate, uint16_t channels) {
    uint32_t data_size = static_cast<uint32_t>(sample_count * sizeof(float));
    uint32_t file_size = 44 + data_size - 8;
    uint16_t format_tag = 3; // IEEE float
    uint16_t block_align = channels * sizeof(float);
    uint32_t byte_rate = sample_rate * block_align;
    uint16_t bits_per_sample = 32;

    std::vector<uint8_t> wav(44 + data_size);
    auto w16 = [](uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); };
    auto w32 = [](uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); };

    std::memcpy(&wav[0], "RIFF", 4);
    w32(&wav[4], file_size);
    std::memcpy(&wav[8], "WAVE", 4);
    std::memcpy(&wav[12], "fmt ", 4);
    w32(&wav[16], 16);
    w16(&wav[20], format_tag);
    w16(&wav[22], channels);
    w32(&wav[24], sample_rate);
    w32(&wav[28], byte_rate);
    w16(&wav[32], block_align);
    w16(&wav[34], bits_per_sample);
    std::memcpy(&wav[36], "data", 4);
    w32(&wav[40], data_size);
    std::memcpy(&wav[44], samples, data_size);
    return wav;
}

// ---------------------------------------------------------------------------
// stb_image_write callback for in-memory PNG
// ---------------------------------------------------------------------------

static void stbi_write_to_vec(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

// ---------------------------------------------------------------------------
// Half-float (IEEE 754 binary16) to float conversion
// ---------------------------------------------------------------------------

static float half_to_float(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x03FF;
    uint32_t f;
    if (exp == 0) {
        if (mant == 0) { f = sign; }
        else {
            exp = 1;
            while (!(mant & 0x0400)) { mant <<= 1; exp--; }
            mant &= 0x03FF;
            f = sign | ((exp + 112) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        f = sign | 0x7F800000u | (mant << 13);
    } else {
        f = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float result;
    std::memcpy(&result, &f, 4);
    return result;
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

static std::string capture_json_err(const std::string& msg) {
    return R"({"ok":false,"error":")" + json_escape(msg) + "\"}";
}

// Forward-decl for use by tick_plots() above its (existing) definition site.
static std::string float_str(float v);

// ---------------------------------------------------------------------------
// GPU readback helper: texture → RGBA8 pixels
// Creates its own encoder, submits, and waits synchronously.
// ---------------------------------------------------------------------------

static bool gpu_readback_rgba8(WGPUDevice device, WGPUQueue queue,
                                WGPUTexture texture, uint32_t w, uint32_t h,
                                std::vector<uint8_t>& out_pixels) {
    if (!texture) return false;

    const uint32_t src_bpp = 8; // RGBA16Float
    const uint32_t unpadded_row = w * src_bpp;
    static constexpr uint32_t kGpuRowAlignment = 256;
    const uint32_t aligned_row = (unpadded_row + kGpuRowAlignment - 1) & ~(kGpuRowAlignment - 1);
    const uint64_t buf_size = static_cast<uint64_t>(aligned_row) * h;

    WGPUBufferDescriptor staging_desc{};
    staging_desc.label = to_sv("Capture Staging");
    staging_desc.size = buf_size;
    staging_desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    staging_desc.mappedAtCreation = false;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(device, &staging_desc);
    if (!staging) return false;

    // Create a dedicated encoder for the copy
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = to_sv("Capture Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);

    WGPUTexelCopyTextureInfo src{};
    src.texture = texture;
    src.mipLevel = 0;
    src.origin = { 0, 0, 0 };
    src.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.offset = 0;
    dst.layout.bytesPerRow = aligned_row;
    dst.layout.rowsPerImage = h;

    WGPUExtent3D copy_size = { w, h, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copy_size);

    gpu_submit(device, queue, encoder, "Capture Commands");

    // Wait for GPU work
    {
        bool work_done = false;
        WGPUQueueWorkDoneCallbackInfo work_cb{};
        work_cb.mode = WGPUCallbackMode_AllowSpontaneous;
        work_cb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* ud1, void*) {
            *static_cast<bool*>(ud1) = true;
        };
        work_cb.userdata1 = &work_done;
        wgpuQueueOnSubmittedWorkDone(queue, work_cb);
        while (!work_done)
            wgpuDevicePoll(device, true, nullptr);
    }

    // Map staging buffer
    bool map_done = false;
    WGPUBufferMapCallbackInfo map_cb{};
    map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    map_cb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* ud1, void*) {
        *static_cast<bool*>(ud1) = true;
    };
    map_cb.userdata1 = &map_done;
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, buf_size, map_cb);
    while (!map_done)
        wgpuDevicePoll(device, true, nullptr);

    const uint8_t* mapped = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(staging, 0, buf_size));

    // Convert RGBA16Float → RGBA8 with linear→sRGB gamma
    out_pixels.resize(w * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        const uint8_t* src_row = mapped + y * aligned_row;
        uint8_t* dst_row = out_pixels.data() + y * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            const uint16_t* fp16 = reinterpret_cast<const uint16_t*>(src_row + x * 8);
            for (int c = 0; c < 4; ++c) {
                float v = half_to_float(fp16[c]);
                if (c < 3) v = std::pow(std::max(0.0f, std::min(1.0f, v)), 1.0f / 2.2f);
                else v = std::max(0.0f, std::min(1.0f, v));
                dst_row[x * 4 + c] = static_cast<uint8_t>(v * 255.0f + 0.5f);
            }
        }
    }

    wgpuBufferUnmap(staging);
    wgpuBufferRelease(staging);
    return true;
}

// ---------------------------------------------------------------------------
// CaptureCoordinator public API
// ---------------------------------------------------------------------------

std::future<std::string> CaptureCoordinator::request_capture(CaptureType type, float audio_duration) {
    CaptureRequest req;
    req.type = type;
    req.audio_duration = audio_duration;
    auto future = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(std::move(req));
    }
    return future;
}

std::future<std::string> CaptureCoordinator::request_start_recording(const std::string& path, double fps) {
    CaptureRequest req;
    req.type = CaptureType::StartRecording;
    req.recording_path = path;
    req.recording_fps = fps;
    auto future = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(std::move(req));
    }
    return future;
}

std::future<std::string> CaptureCoordinator::request_stop_recording() {
    CaptureRequest req;
    req.type = CaptureType::StopRecording;
    auto future = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(std::move(req));
    }
    return future;
}

bool CaptureCoordinator::has_pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty() || !pending_analysis_requests_.empty() || !pending_compare_requests_.empty();
}

bool CaptureCoordinator::has_pending_analyses() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_analyses_.empty();
}

std::future<std::string> CaptureCoordinator::request_analyze(AnalysisMode mode, float window_seconds,
                                                               bool include_payload, const std::string& node_id) {
    AnalysisRequest req;
    req.mode = mode;
    req.window_seconds = window_seconds;
    req.include_payload = include_payload;
    req.node_id = node_id;
    auto future = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_analysis_requests_.push_back(std::move(req));
    }
    return future;
}

std::future<std::string> CaptureCoordinator::request_compare(AnalysisMode mode, float window_a, float window_b,
                                                               bool include_payload, const std::string& node_id) {
    CompareRequest req;
    req.mode = mode;
    req.window_a = window_a;
    req.window_b = window_b;
    req.include_payload = include_payload;
    req.node_id = node_id;
    auto future = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_compare_requests_.push_back(std::move(req));
    }
    return future;
}

// ===========================================================================
// P1 (pivot) — minimal data-only endpoints. Python librosa-based MCP tools
// sit on top of these. No analysis, no rendering — raw bytes / arrays only.
// ===========================================================================

// ---------------------------------------------------------------------------
// capture_node_audio — synchronous read of a named node's 1024-sample
// waveform ring, encoded as 32-bit float WAV (base64). ~21 ms @ 48 kHz.
// ---------------------------------------------------------------------------

std::string CaptureCoordinator::handle_capture_node_audio(const std::string& node_id,
                                                           int channel) {
    if (!audio_) return capture_json_err("no audio engine");
    int idx = audio_->audio_node_index(node_id);
    if (idx < 0) {
        return capture_json_err("node not found or not audio-cadence: " + node_id);
    }
    const auto& snap = audio_->analysis_read();
    if (static_cast<size_t>(idx) >= snap.waveform.size() ||
        static_cast<size_t>(idx) >= snap.channel_counts.size()) {
        return capture_json_err("no waveform snapshot for node: " + node_id);
    }
    uint16_t channels = snap.channel_counts[idx];
    if (channels == 0) channels = 1;
    if (channels > AnalysisSnapshot::kMaxWaveformChannels)
        channels = AnalysisSnapshot::kMaxWaveformChannels;
    const uint32_t kRing = AnalysisSnapshot::kWaveformSamples;

    // Optionally restrict to a single channel.
    uint16_t out_channels = channels;
    std::vector<float> samples;
    if (channel >= 0 && channel < channels) {
        out_channels = 1;
        samples.resize(kRing);
        for (uint32_t i = 0; i < kRing; ++i) {
            samples[i] = snap.waveform[idx][channel][i];
        }
    } else {
        samples.resize(static_cast<size_t>(kRing) * channels);
        for (uint32_t i = 0; i < kRing; ++i) {
            for (uint16_t c = 0; c < channels; ++c) {
                samples[static_cast<size_t>(i) * channels + c] =
                    snap.waveform[idx][c][i];
            }
        }
    }

    const uint32_t rate = AudioEngine::kSampleRate;
    auto wav = encode_wav_float32(samples.data(), samples.size(), rate, out_channels);
    std::string b64 = base64_encode(wav.data(), wav.size());
    std::string json = R"({"ok":true,"node_id":")" + json_escape(node_id) + "\"";
    json += R"(,"sample_rate":)" + std::to_string(rate);
    json += R"(,"channels":)" + std::to_string(out_channels);
    json += R"(,"frames":)" + std::to_string(kRing);
    json += R"(,"wav_base64":")" + b64 + "\"}";
    return json;
}

// ---------------------------------------------------------------------------
// capture_lane_series — sample a node's lane-array output port every frame
// for `duration_ms` and return the raw per-lane time series as JSON.
// Optional id_port_name collects a parallel id stream so Python can color
// stripes by stable voice identity.
// ---------------------------------------------------------------------------

std::future<std::string> CaptureCoordinator::request_lane_series(
    const std::string& node_id, const std::string& port_name,
    const std::string& id_port_name, float duration_ms) {
    LaneSeriesRequest req;
    req.node_id = node_id;
    req.port_name = port_name;
    req.id_port_name = id_port_name;
    req.duration_ms = std::clamp(duration_ms, 50.0f, 10000.0f);
    auto fut = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_lane_series_requests_.push_back(std::move(req));
    }
    return fut;
}

void CaptureCoordinator::tick_lane_series() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& req : pending_lane_series_requests_) {
            PendingLaneSeries pl;
            pl.req = std::move(req);
            pending_lane_series_.push_back(std::move(pl));
        }
        pending_lane_series_requests_.clear();
    }

    auto now = std::chrono::steady_clock::now();
    auto it = pending_lane_series_.begin();
    while (it != pending_lane_series_.end()) {
        auto& pl = *it;
        try {
            if (!pl.started) {
                pl.start_time = now;
                pl.started = true;
                if (!audio_) {
                    pl.req.promise.set_value(capture_json_err("no audio engine"));
                    it = pending_lane_series_.erase(it);
                    continue;
                }
                int port_idx = -1;
                bool is_lane = false;
                audio_->audio_node_output_port(pl.req.node_id, pl.req.port_name,
                                                &port_idx, &is_lane);
                if (port_idx < 0) {
                    pl.req.promise.set_value(capture_json_err(
                        "node/port not found or not audio-cadence: " +
                        pl.req.node_id + "/" + pl.req.port_name));
                    it = pending_lane_series_.erase(it);
                    continue;
                }
                pl.audio_node_idx = audio_->audio_node_index(pl.req.node_id);
                pl.port_idx = port_idx;
                if (!pl.req.id_port_name.empty()) {
                    int idp = -1; bool dummy = false;
                    audio_->audio_node_output_port(pl.req.node_id, pl.req.id_port_name,
                                                    &idp, &dummy);
                    pl.id_port_idx = idp;  // -1 if missing — silently ignored
                }
            }

            // Sample this frame.
            const auto& snap = audio_->analysis_read();
            if (static_cast<size_t>(pl.audio_node_idx) < snap.lane_outputs.size() &&
                static_cast<size_t>(pl.port_idx) < snap.lane_outputs[pl.audio_node_idx].size()) {
                const auto& slot = snap.lane_outputs[pl.audio_node_idx][pl.port_idx];
                uint32_t n = slot.length;
                if (n > pl.observed_lane_count) {
                    pl.per_lane_samples.resize(n);
                    if (pl.id_port_idx >= 0) pl.per_lane_ids.resize(n);
                    pl.observed_lane_count = n;
                }
                size_t prev_len = pl.per_lane_samples.empty()
                    ? 0 : pl.per_lane_samples[0].size();
                const float* id_data = nullptr;
                uint32_t id_len = 0;
                if (pl.id_port_idx >= 0 &&
                    static_cast<size_t>(pl.id_port_idx) <
                        snap.lane_outputs[pl.audio_node_idx].size()) {
                    const auto& id_slot = snap.lane_outputs[pl.audio_node_idx][pl.id_port_idx];
                    id_data = id_slot.data;
                    id_len = id_slot.length;
                }
                for (uint32_t i = 0; i < pl.observed_lane_count; ++i) {
                    float v = (i < n && slot.data) ? slot.data[i] : 0.0f;
                    pl.per_lane_samples[i].push_back(v);
                    if (pl.per_lane_samples[i].size() < prev_len + 1) {
                        pl.per_lane_samples[i].resize(prev_len, 0.0f);
                        pl.per_lane_samples[i].push_back(v);
                    }
                    if (pl.id_port_idx >= 0) {
                        uint32_t id = (i < id_len && id_data)
                            ? static_cast<uint32_t>(id_data[i]) : 0;
                        pl.per_lane_ids[i].push_back(id);
                        if (pl.per_lane_ids[i].size() < prev_len + 1) {
                            pl.per_lane_ids[i].resize(prev_len, 0);
                            pl.per_lane_ids[i].push_back(id);
                        }
                    }
                }
            }

            float elapsed_ms = std::chrono::duration<float, std::milli>(
                now - pl.start_time).count();
            if (elapsed_ms < pl.req.duration_ms) {
                ++it;
                continue;
            }

            // Done — serialize raw arrays.
            uint32_t lanes = static_cast<uint32_t>(pl.per_lane_samples.size());
            std::string json = R"({"ok":true,"node_id":")" + json_escape(pl.req.node_id) + "\"";
            json += R"(,"port_name":")" + json_escape(pl.req.port_name) + "\"";
            if (!pl.req.id_port_name.empty())
                json += R"(,"id_port_name":")" + json_escape(pl.req.id_port_name) + "\"";
            json += R"(,"lane_count":)" + std::to_string(lanes);
            json += R"(,"samples_per_lane":)" + std::to_string(
                lanes > 0 ? pl.per_lane_samples[0].size() : 0);
            json += R"(,"duration_ms":)" + float_str(pl.req.duration_ms);
            json += R"(,"samples":[)";
            for (uint32_t i = 0; i < lanes; ++i) {
                if (i > 0) json += ",";
                json += "[";
                for (size_t k = 0; k < pl.per_lane_samples[i].size(); ++k) {
                    if (k > 0) json += ",";
                    json += float_str(pl.per_lane_samples[i][k]);
                }
                json += "]";
            }
            json += "]";
            if (!pl.req.id_port_name.empty()) {
                json += R"(,"ids":[)";
                for (uint32_t i = 0; i < lanes; ++i) {
                    if (i > 0) json += ",";
                    json += "[";
                    for (size_t k = 0; k < pl.per_lane_ids[i].size(); ++k) {
                        if (k > 0) json += ",";
                        json += std::to_string(pl.per_lane_ids[i][k]);
                    }
                    json += "]";
                }
                json += "]";
            }
            json += "}";
            pl.req.promise.set_value(json);
            it = pending_lane_series_.erase(it);
        } catch (const std::exception& e) {
            pl.req.promise.set_value(capture_json_err(e.what()));
            it = pending_lane_series_.erase(it);
        } catch (...) {
            pl.req.promise.set_value(capture_json_err("unknown error during lane series capture"));
            it = pending_lane_series_.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// capture_note_window — atomic inject + audio-window capture. Replaces
// {capture_note_response, capture_polyphony_response, capture_retrigger_response}
// at the C++ HTTP layer; Python wrappers build the events[] schedule and
// run librosa on the returned WAV.
// ---------------------------------------------------------------------------

std::future<std::string> CaptureCoordinator::request_note_window(NoteWindowRequest req) {
    req.capture_ms = std::clamp(req.capture_ms, 50.0f, 30000.0f);
    auto fut = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_note_window_requests_.push_back(std::move(req));
    }
    return fut;
}

void CaptureCoordinator::tick_note_window() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& req : pending_note_window_requests_) {
            PendingNoteWindow pn;
            pn.req = std::move(req);
            pending_note_windows_.push_back(std::move(pn));
        }
        pending_note_window_requests_.clear();
    }

    auto now = std::chrono::steady_clock::now();
    auto it = pending_note_windows_.begin();
    while (it != pending_note_windows_.end()) {
        auto& pn = *it;
        try {
            if (!pn.started) {
                if (!runtime_api_) {
                    pn.req.promise.set_value(capture_json_err("no runtime api"));
                    it = pending_note_windows_.erase(it);
                    continue;
                }
                if (audio_) {
                    audio_->start_recording_tap();
                    uint64_t avail = audio_->available_recorded_samples();
                    if (avail > 0) {
                        std::vector<float> drain(avail);
                        audio_->pop_recorded_samples(drain.data(), avail);
                    }
                }
                // Optional lane data setup.
                if (audio_ && !pn.req.lane_node_id.empty() && !pn.req.lane_port_name.empty()) {
                    int port_idx = -1; bool dummy = false;
                    audio_->audio_node_output_port(pn.req.lane_node_id,
                                                    pn.req.lane_port_name,
                                                    &port_idx, &dummy);
                    if (port_idx >= 0) {
                        pn.lane_audio_node_idx = audio_->audio_node_index(pn.req.lane_node_id);
                        pn.lane_port_idx = port_idx;
                        if (!pn.req.lane_id_port_name.empty()) {
                            int idp = -1; bool d2 = false;
                            audio_->audio_node_output_port(pn.req.lane_node_id,
                                                            pn.req.lane_id_port_name,
                                                            &idp, &d2);
                            pn.lane_id_port_idx = idp;
                        }
                    }
                }
                pn.start_time = now;
                pn.started = true;
            }

            float elapsed_ms = std::chrono::duration<float, std::milli>(
                now - pn.start_time).count();

            // Fire any due events.
            while (pn.next_event < pn.req.events.size() &&
                   pn.req.events[pn.next_event].t_ms <= elapsed_ms) {
                const auto& ev = pn.req.events[pn.next_event];
                if (ev.length > 0 && runtime_api_) {
                    bool ok = runtime_api_->inject_midi_to_node(
                        pn.req.midi_node_id, ev.bytes, ev.length);
                    if (!ok && pn.next_event == 0) {
                        pn.req.promise.set_value(capture_json_err(
                            "midi_node_id missing or operator does not export "
                            "vivid_op_inject_midi: " + pn.req.midi_node_id));
                        it = pending_note_windows_.erase(it);
                        goto continue_outer_nw;
                    }
                }
                ++pn.next_event;
            }

            // Lane sampling each tick (if requested).
            if (pn.lane_audio_node_idx >= 0 && audio_) {
                const auto& snap = audio_->analysis_read();
                if (static_cast<size_t>(pn.lane_audio_node_idx) < snap.lane_outputs.size() &&
                    static_cast<size_t>(pn.lane_port_idx) <
                        snap.lane_outputs[pn.lane_audio_node_idx].size()) {
                    const auto& slot = snap.lane_outputs[pn.lane_audio_node_idx][pn.lane_port_idx];
                    uint32_t n = slot.length;
                    if (n > pn.observed_lane_count) {
                        pn.per_lane_samples.resize(n);
                        if (pn.lane_id_port_idx >= 0) pn.per_lane_ids.resize(n);
                        pn.observed_lane_count = n;
                    }
                    size_t prev_len = pn.per_lane_samples.empty()
                        ? 0 : pn.per_lane_samples[0].size();
                    const float* id_data = nullptr;
                    uint32_t id_len = 0;
                    if (pn.lane_id_port_idx >= 0 &&
                        static_cast<size_t>(pn.lane_id_port_idx) <
                            snap.lane_outputs[pn.lane_audio_node_idx].size()) {
                        const auto& id_slot = snap.lane_outputs[pn.lane_audio_node_idx][pn.lane_id_port_idx];
                        id_data = id_slot.data;
                        id_len = id_slot.length;
                    }
                    for (uint32_t i = 0; i < pn.observed_lane_count; ++i) {
                        float v = (i < n && slot.data) ? slot.data[i] : 0.0f;
                        pn.per_lane_samples[i].push_back(v);
                        if (pn.per_lane_samples[i].size() < prev_len + 1) {
                            pn.per_lane_samples[i].resize(prev_len, 0.0f);
                            pn.per_lane_samples[i].push_back(v);
                        }
                        if (pn.lane_id_port_idx >= 0) {
                            uint32_t id = (i < id_len && id_data)
                                ? static_cast<uint32_t>(id_data[i]) : 0;
                            pn.per_lane_ids[i].push_back(id);
                            if (pn.per_lane_ids[i].size() < prev_len + 1) {
                                pn.per_lane_ids[i].resize(prev_len, 0);
                                pn.per_lane_ids[i].push_back(id);
                            }
                        }
                    }
                }
            }

            if (elapsed_ms < pn.req.capture_ms) {
                ++it;
                continue;
            }

            // Pop audio samples — final-mix tap or per-node ring snapshot.
            std::vector<float> samples;
            uint64_t popped = 0;
            uint16_t channels = 2;
            const uint32_t rate = AudioEngine::kSampleRate;
            if (!pn.req.audio_node_id.empty() && audio_) {
                int idx = audio_->audio_node_index(pn.req.audio_node_id);
                if (idx >= 0) {
                    const auto& snap = audio_->analysis_read();
                    if (static_cast<size_t>(idx) < snap.waveform.size() &&
                        static_cast<size_t>(idx) < snap.channel_counts.size()) {
                        uint16_t ch = snap.channel_counts[idx];
                        if (ch == 0) ch = 1;
                        if (ch > AnalysisSnapshot::kMaxWaveformChannels)
                            ch = AnalysisSnapshot::kMaxWaveformChannels;
                        const uint32_t kRing = AnalysisSnapshot::kWaveformSamples;
                        channels = ch;
                        samples.resize(static_cast<size_t>(kRing) * ch);
                        for (uint32_t i = 0; i < kRing; ++i)
                            for (uint16_t c = 0; c < ch; ++c)
                                samples[static_cast<size_t>(i) * ch + c] = snap.waveform[idx][c][i];
                        popped = static_cast<uint64_t>(kRing) * ch;
                    }
                }
            } else if (audio_) {
                uint64_t avail = audio_->available_recorded_samples();
                if (avail > 0) {
                    samples.resize(avail);
                    popped = audio_->pop_recorded_samples(samples.data(), avail);
                    samples.resize(popped);
                }
            }
            uint64_t frame_count = popped / channels;

            auto wav = encode_wav_float32(samples.data(), popped, rate, channels);
            std::string b64 = base64_encode(wav.data(), wav.size());

            std::string json = R"({"ok":true,"midi_node_id":")" + json_escape(pn.req.midi_node_id) + "\"";
            json += R"(,"events_scheduled":)" + std::to_string(pn.req.events.size());
            json += R"(,"events_fired":)" + std::to_string(pn.next_event);
            json += R"(,"capture_ms":)" + float_str(pn.req.capture_ms);
            json += R"(,"sample_rate":)" + std::to_string(rate);
            json += R"(,"channels":)" + std::to_string(channels);
            json += R"(,"frames":)" + std::to_string(frame_count);
            json += R"(,"audio_source":")"
                + std::string(pn.req.audio_node_id.empty() ? "final_mix_tap" : "node_waveform_ring")
                + "\"";
            if (!pn.req.audio_node_id.empty())
                json += R"(,"audio_node_id":")" + json_escape(pn.req.audio_node_id) + "\"";
            json += R"(,"wav_base64":")" + b64 + "\"";

            // Optional lane data side-channel.
            if (pn.lane_audio_node_idx >= 0) {
                uint32_t lanes = static_cast<uint32_t>(pn.per_lane_samples.size());
                json += R"(,"lane_data":{"node_id":")" + json_escape(pn.req.lane_node_id) + "\"";
                json += R"(,"port_name":")" + json_escape(pn.req.lane_port_name) + "\"";
                if (!pn.req.lane_id_port_name.empty())
                    json += R"(,"id_port_name":")" + json_escape(pn.req.lane_id_port_name) + "\"";
                json += R"(,"lane_count":)" + std::to_string(lanes);
                json += R"(,"samples_per_lane":)" + std::to_string(
                    lanes > 0 ? pn.per_lane_samples[0].size() : 0);
                json += R"(,"samples":[)";
                for (uint32_t i = 0; i < lanes; ++i) {
                    if (i > 0) json += ",";
                    json += "[";
                    for (size_t k = 0; k < pn.per_lane_samples[i].size(); ++k) {
                        if (k > 0) json += ",";
                        json += float_str(pn.per_lane_samples[i][k]);
                    }
                    json += "]";
                }
                json += "]";
                if (!pn.req.lane_id_port_name.empty()) {
                    json += R"(,"ids":[)";
                    for (uint32_t i = 0; i < lanes; ++i) {
                        if (i > 0) json += ",";
                        json += "[";
                        for (size_t k = 0; k < pn.per_lane_ids[i].size(); ++k) {
                            if (k > 0) json += ",";
                            json += std::to_string(pn.per_lane_ids[i][k]);
                        }
                        json += "]";
                    }
                    json += "]";
                }
                json += "}";
            }

            json += "}";
            pn.req.promise.set_value(json);
            it = pending_note_windows_.erase(it);
        } catch (const std::exception& e) {
            pn.req.promise.set_value(capture_json_err(e.what()));
            it = pending_note_windows_.erase(it);
        } catch (...) {
            pn.req.promise.set_value(capture_json_err("unknown error during note window"));
            it = pending_note_windows_.erase(it);
        }
        continue_outer_nw:;
    }
}

std::string CaptureCoordinator::handle_start_recording_tap() {
    if (!audio_)
        return capture_json_err("no audio engine available");
    audio_->start_recording_tap();
    return R"({"ok":true,"message":"recording tap started"})";
}

std::string CaptureCoordinator::handle_stop_recording_tap() {
    if (!audio_)
        return capture_json_err("no audio engine available");
    audio_->stop_recording_tap();
    return R"({"ok":true,"message":"recording tap stopped"})";
}

void CaptureCoordinator::process_pending(WGPUDevice device, WGPUQueue queue,
                                          WGPUTexture capture_tex,
                                          uint32_t tex_width, uint32_t tex_height) {
    std::vector<CaptureRequest> requests;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requests.swap(pending_);
    }

    for (auto& req : requests) {
        try {
            std::string response;
            switch (req.type) {
            case CaptureType::Frame:
                response = capture_frame(device, queue, capture_tex, tex_width, tex_height);
                break;
            case CaptureType::Audio:
                response = capture_audio(req.audio_duration);
                break;
            case CaptureType::AV: {
                // Both in one response
                std::string json = R"({"ok":true)";
                bool have_any = false;

                if (capture_tex && tex_width > 0 && tex_height > 0) {
                    std::vector<uint8_t> pixels;
                    if (gpu_readback_rgba8(device, queue, capture_tex, tex_width, tex_height, pixels)) {
                        std::vector<uint8_t> png_data;
                        png_data.reserve(tex_width * tex_height);
                        stbi_write_png_to_func(stbi_write_to_vec, &png_data,
                                               tex_width, tex_height, 4, pixels.data(), tex_width * 4);
                        if (!png_data.empty()) {
                            std::string b64 = base64_encode(png_data.data(), png_data.size());
                            json += R"(,"width":)" + std::to_string(tex_width);
                            json += R"(,"height":)" + std::to_string(tex_height);
                            json += R"(,"png_base64":")" + b64 + "\"";
                            have_any = true;
                        }
                    }
                }

                if (audio_) {
                    uint64_t sample_count = static_cast<uint64_t>(req.audio_duration * AudioEngine::kSampleRate) * 2;
                    uint64_t avail = audio_->available_recorded_samples();
                    if (sample_count > avail) sample_count = avail;
                    if (sample_count > 0) {
                        std::vector<float> samples(sample_count);
                        uint64_t popped = audio_->pop_recorded_samples(samples.data(), sample_count);
                        samples.resize(popped);
                        auto wav = encode_wav_float32(samples.data(), popped, AudioEngine::kSampleRate, 2);
                        std::string b64 = base64_encode(wav.data(), wav.size());
                        json += R"(,"sample_rate":)" + std::to_string(AudioEngine::kSampleRate);
                        json += R"(,"channels":2)";
                        json += R"(,"samples":)" + std::to_string(popped / 2);
                        json += R"(,"wav_base64":")" + b64 + "\"";
                        have_any = true;
                    }
                }

                json += "}";
                response = have_any ? json : capture_json_err("no video or audio output available");
                break;
            }
            case CaptureType::StartRecording: {
                if (exporter_->is_recording()) {
                    response = capture_json_err("already recording");
                } else if (!capture_tex || tex_width == 0 || tex_height == 0) {
                    response = capture_json_err("no video output available for recording");
                } else {
                    // Auto-start recording tap for audio
                    if (audio_) audio_->start_recording_tap();

                    if (exporter_->start(req.recording_path, tex_width, tex_height,
                                         req.recording_fps, AudioEngine::kSampleRate)) {
                        response = R"({"ok":true,"message":"recording started"})";
                    } else {
                        if (audio_) audio_->stop_recording_tap();
                        response = capture_json_err("failed to start recording");
                    }
                }
                break;
            }
            case CaptureType::StopRecording: {
                if (!exporter_->is_recording()) {
                    response = capture_json_err("not recording");
                } else {
                    std::string path = exporter_->output_path();
                    bool ok = exporter_->finish();
                    if (audio_) audio_->stop_recording_tap();
                    if (ok) {
                        response = R"({"ok":true,"path":")" + json_escape(path) + "\"}";
                    } else {
                        response = R"({"ok":false,"error":"failed to finalize recording","path":")" +
                                   json_escape(path) + "\"}";
                    }
                }
                break;
            }
            case CaptureType::SnapshotToFile: {
                if (!capture_tex || tex_width == 0 || tex_height == 0) {
                    response = capture_json_err("no video output available");
                    break;
                }
                std::vector<uint8_t> pixels;
                if (!gpu_readback_rgba8(device, queue, capture_tex, tex_width, tex_height, pixels)) {
                    response = capture_json_err("GPU readback failed");
                    break;
                }
                std::string out_path = req.recording_path;
                if (out_path.empty()) {
                    // Generate default path: ~/Desktop/vivid_snapshot_YYYYMMDD_HHMMSS.png
                    const char* home = std::getenv("HOME");
                    std::string desktop = home ? std::string(home) + "/Desktop" : ".";
                    std::time_t t = std::time(nullptr);
                    std::tm tm{};
                    localtime_r(&t, &tm);
                    char ts[32];
                    std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);
                    out_path = desktop + "/vivid_snapshot_" + ts + ".png";
                }
                if (stbi_write_png(out_path.c_str(), tex_width, tex_height, 4,
                                   pixels.data(), tex_width * 4)) {
                    response = R"({"ok":true,"path":")" + json_escape(out_path) + "\"}";
                    std::fprintf(stderr, "[vivid] Snapshot saved: %s\n", out_path.c_str());
                } else {
                    response = capture_json_err("failed to write PNG");
                }
                break;
            }
            }
            req.promise.set_value(std::move(response));
        } catch (const std::exception& e) {
            req.promise.set_value(capture_json_err(e.what()));
        } catch (...) {
            req.promise.set_value(capture_json_err("unknown internal error"));
        }
    }
}

// ---------------------------------------------------------------------------
// tick_recording — feed video/audio to AVExporter each frame
// ---------------------------------------------------------------------------

bool CaptureCoordinator::is_recording() const {
    return exporter_ && exporter_->is_recording();
}

uint64_t CaptureCoordinator::recording_frame_count() const {
    if (!exporter_ || !exporter_->is_recording()) return 0;
    return exporter_->frame_count();
}

double CaptureCoordinator::recording_duration_sec() const {
    if (!exporter_ || !exporter_->is_recording()) return 0.0;
    return exporter_->elapsed_sec();
}

std::future<std::string> CaptureCoordinator::request_snapshot_to_file(const std::string& path) {
    CaptureRequest req;
    req.type = CaptureType::SnapshotToFile;
    req.recording_path = path;
    auto future = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(std::move(req));
    }
    return future;
}

std::future<std::string> CaptureCoordinator::request_interface_capture(const std::string& node_id,
                                                                       const std::string& save_path,
                                                                       bool ensure_ui_visible) {
    InterfaceCaptureRequest req;
    req.node_id = node_id;
    req.save_path = save_path;
    req.ensure_ui_visible = ensure_ui_visible;
    auto future = req.promise.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_interface_capture_requests_.push_back(std::move(req));
    }
    return future;
}

bool CaptureCoordinator::has_pending_interface_capture() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_interface_capture_requests_.empty();
}

bool CaptureCoordinator::has_active_interface_capture() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_interface_capture_.has_value();
}

bool CaptureCoordinator::prepare_pending_interface_capture(ui::NodeGraphUI& graph_ui) {
    for (;;) {
        std::string node_id;
        bool ensure_ui_visible = true;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!active_interface_capture_) {
                if (pending_interface_capture_requests_.empty())
                    return false;
                active_interface_capture_ = std::move(pending_interface_capture_requests_.front());
                pending_interface_capture_requests_.erase(pending_interface_capture_requests_.begin());
            }
            node_id = active_interface_capture_->node_id;
            ensure_ui_visible = active_interface_capture_->ensure_ui_visible;
        }

        if (ensure_ui_visible)
            graph_ui.set_visible(true);

        if (!node_id.empty() && !graph_ui.select_single_node_for_review(node_id)) {
            fail_active_interface_capture("node id '" + node_id + "' not found");
            continue;
        }
        return true;
    }
}

void CaptureCoordinator::complete_active_interface_capture(uint32_t width, uint32_t height,
                                                           const std::vector<uint8_t>& png_data) {
    std::optional<InterfaceCaptureRequest> req;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_interface_capture_)
            return;
        req = std::move(active_interface_capture_);
        active_interface_capture_.reset();
    }

    if (!req)
        return;

    std::string response;
    if (png_data.empty()) {
        response = capture_json_err("PNG encoding failed");
    } else {
        std::string saved_path;
        if (!req->save_path.empty()) {
            std::ofstream out(req->save_path, std::ios::binary);
            if (!out) {
                response = capture_json_err("failed to open save_path for writing");
            } else {
                out.write(reinterpret_cast<const char*>(png_data.data()),
                          static_cast<std::streamsize>(png_data.size()));
                if (!out.good()) {
                    response = capture_json_err("failed to write PNG");
                } else {
                    saved_path = req->save_path;
                }
            }
        }

        if (response.empty()) {
            if (!saved_path.empty()) {
                // The PNG was written to disk; return only the path. Omitting the
                // inline base64 keeps the response small (a full-frame PNG easily
                // exceeds MCP tool-result token limits) and skips the encode cost.
                response = R"({"ok":true,"width":)" + std::to_string(width) +
                           R"(,"height":)" + std::to_string(height) +
                           R"(,"path":")" + json_escape(saved_path) + "\"}";
            } else {
                // No save_path: deliver the image inline as base64.
                std::string b64 = base64_encode(png_data.data(), png_data.size());
                response = R"({"ok":true,"width":)" + std::to_string(width) +
                           R"(,"height":)" + std::to_string(height) +
                           R"(,"png_base64":")" + b64 + "\"}";
            }
        }
    }

    req->promise.set_value(std::move(response));
}

void CaptureCoordinator::fail_active_interface_capture(const std::string& error) {
    std::optional<InterfaceCaptureRequest> req;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_interface_capture_)
            return;
        req = std::move(active_interface_capture_);
        active_interface_capture_.reset();
    }
    if (req)
        req->promise.set_value(capture_json_err(error));
}

void CaptureCoordinator::fail_pending_interface_captures(const std::string& error) {
    std::vector<std::promise<std::string>> pending_promises;
    std::optional<std::promise<std::string>> active_promise;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& req : pending_interface_capture_requests_)
            pending_promises.push_back(std::move(req.promise));
        pending_interface_capture_requests_.clear();
        if (active_interface_capture_) {
            active_promise = std::move(active_interface_capture_->promise);
            active_interface_capture_.reset();
        }
    }

    std::string response = capture_json_err(error);
    for (auto& promise : pending_promises)
        promise.set_value(response);
    if (active_promise)
        active_promise->set_value(std::move(response));
}

void CaptureCoordinator::tick_recording(WGPUDevice device, WGPUQueue queue,
                                         WGPUTexture capture_tex,
                                         uint32_t tex_width, uint32_t tex_height) {
    if (!exporter_->is_recording()) return;

    // Video frame
    if (capture_tex && tex_width > 0 && tex_height > 0) {
        std::vector<uint8_t> pixels;
        if (gpu_readback_rgba8(device, queue, capture_tex, tex_width, tex_height, pixels)) {
            exporter_->write_video_frame(pixels.data(), tex_width, tex_height);
        }
    }

    // Audio samples
    if (audio_) {
        uint64_t avail = audio_->available_recorded_samples();
        if (avail > 0) {
            std::vector<float> samples(avail);
            uint64_t popped = audio_->pop_recorded_samples(samples.data(), avail);
            if (popped > 0) {
                exporter_->write_audio_samples(samples.data(), popped, 2);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Frame capture
// ---------------------------------------------------------------------------

std::string CaptureCoordinator::capture_frame(WGPUDevice device, WGPUQueue queue,
                                               WGPUTexture tex,
                                               uint32_t w, uint32_t h) {
    if (!tex || w == 0 || h == 0)
        return capture_json_err("no video output available");

    std::vector<uint8_t> pixels;
    if (!gpu_readback_rgba8(device, queue, tex, w, h, pixels))
        return capture_json_err("GPU readback failed");

    std::vector<uint8_t> png_data;
    png_data.reserve(w * h);
    stbi_write_png_to_func(stbi_write_to_vec, &png_data, w, h, 4, pixels.data(), w * 4);

    if (png_data.empty())
        return capture_json_err("PNG encoding failed");

    std::string b64 = base64_encode(png_data.data(), png_data.size());
    return R"({"ok":true,"width":)" + std::to_string(w) +
           R"(,"height":)" + std::to_string(h) +
           R"(,"png_base64":")" + b64 + "\"}";
}

// ---------------------------------------------------------------------------
// Audio capture
// ---------------------------------------------------------------------------

std::string CaptureCoordinator::capture_audio(float duration) {
    if (!audio_)
        return capture_json_err("no audio engine available");

    uint64_t sample_count = static_cast<uint64_t>(duration * AudioEngine::kSampleRate) * 2;
    uint64_t avail = audio_->available_recorded_samples();
    if (sample_count > avail) sample_count = avail;
    if (sample_count == 0)
        return capture_json_err("no audio samples available (is recording tap active?)");

    std::vector<float> samples(sample_count);
    uint64_t popped = audio_->pop_recorded_samples(samples.data(), sample_count);
    samples.resize(popped);

    auto wav = encode_wav_float32(samples.data(), popped, AudioEngine::kSampleRate, 2);
    std::string b64 = base64_encode(wav.data(), wav.size());

    return R"({"ok":true,"sample_rate":)" + std::to_string(AudioEngine::kSampleRate) +
           R"(,"channels":2,"samples":)" + std::to_string(popped / 2) +
           R"(,"wav_base64":")" + b64 + "\"}";
}

// ---------------------------------------------------------------------------
// Analysis JSON helpers
// ---------------------------------------------------------------------------

static const char* analysis_mode_str(AnalysisMode m) {
    switch (m) {
    case AnalysisMode::Frame: return "frame";
    case AnalysisMode::Audio: return "audio";
    case AnalysisMode::AV:    return "av";
    }
    return "unknown";
}

static const char* audio_level_label(float rms) {
    if (rms > 0.3f) return "loud";
    if (rms > 0.1f) return "moderate";
    if (rms > 0.001f) return "quiet";
    return "silent";
}

static const char* brightness_label(float b) {
    if (b > 0.8f) return "very_bright";
    if (b > 0.5f) return "bright";
    if (b > 0.2f) return "moderate";
    if (b > 0.05f) return "dark";
    return "very_dark";
}

static const char* contrast_label(float c) {
    if (c > 0.3f) return "high";
    if (c > 0.1f) return "moderate";
    return "low";
}

static const char* motion_label(float m) {
    if (m > 0.3f) return "high";
    if (m > 0.05f) return "moderate";
    if (m > 0.01f) return "low";
    return "none";
}

static std::string float_str(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}

std::string CaptureCoordinator::serialize_analysis(const AnalysisResult& result) {
    std::string json = R"({"ok":true,"schema_version":1,"mode":")" +
        std::string(analysis_mode_str(result.mode)) + "\"";

    // Summary
    json += R"(,"summary":{)";
    bool first_summary = true;
    if (result.mode == AnalysisMode::Audio || result.mode == AnalysisMode::AV) {
        json += R"("audio_level":")" + std::string(audio_level_label(result.audio.rms)) + "\"";
        first_summary = false;
    }
    if (result.mode == AnalysisMode::Frame || result.mode == AnalysisMode::AV) {
        if (!first_summary) json += ",";
        json += R"("brightness":")" + std::string(brightness_label(result.visual.mean_brightness)) + "\"";
        json += R"(,"contrast":")" + std::string(contrast_label(result.visual.contrast)) + "\"";
        json += R"(,"motion":")" + std::string(motion_label(result.visual.motion_magnitude)) + "\"";
    }
    json += "}";

    // Metrics
    json += R"(,"metrics":{)";
    bool first_metric = true;
    if (result.mode == AnalysisMode::Audio || result.mode == AnalysisMode::AV) {
        json += R"("audio":{)";
        json += R"("rms":)" + float_str(result.audio.rms);
        json += R"(,"peak":)" + float_str(result.audio.peak);
        json += R"(,"spectral_centroid_hz":)" + float_str(result.audio.spectral_centroid_hz);
        json += R"(,"spectral_brightness":)" + float_str(result.audio.spectral_brightness);
        json += R"(,"spectral_flatness":)" + float_str(result.audio.spectral_flatness);
        json += "}";
        first_metric = false;
    }
    if (result.mode == AnalysisMode::Frame || result.mode == AnalysisMode::AV) {
        if (!first_metric) json += ",";
        json += R"("visual":{)";
        json += R"("mean_brightness":)" + float_str(result.visual.mean_brightness);
        json += R"(,"contrast":)" + float_str(result.visual.contrast);
        json += R"(,"motion_magnitude":)" + float_str(result.visual.motion_magnitude);
        json += "}";
    }
    if (result.mode == AnalysisMode::AV) {
        json += R"(,"av_reactivity":{)";
        json += R"("energy_brightness_correlation":)" + float_str(result.av_reactivity.energy_brightness_correlation);
        json += R"(,"energy_motion_correlation":)" + float_str(result.av_reactivity.energy_motion_correlation);
        json += R"(,"energy_contrast_correlation":)" + float_str(result.av_reactivity.energy_contrast_correlation);
        json += R"(,"window_seconds":)" + float_str(result.av_reactivity.window_seconds);
        json += R"(,"visual_samples":)" + std::to_string(result.av_reactivity.visual_samples);
        json += R"(,"detected_onsets":)" + std::to_string(result.av_reactivity.detected_onsets);
        json += R"(,"onset_response_rate":)" + float_str(result.av_reactivity.onset_response_rate);
        json += R"(,"reactivity_latency_ms":)" + float_str(result.av_reactivity.reactivity_latency_ms);

        auto band_obj = [&](const char* key, const BandCorrelations& b) {
            json += R"(,")" + std::string(key) + R"(":{)";
            json += R"("bass":)" + float_str(b.bass);
            json += R"(,"mid":)" + float_str(b.mid);
            json += R"(,"treble":)" + float_str(b.treble);
            json += "}";
        };
        band_obj("band_brightness_correlations", result.av_reactivity.band_brightness_correlations);
        band_obj("band_motion_correlations",     result.av_reactivity.band_motion_correlations);
        band_obj("band_contrast_correlations",   result.av_reactivity.band_contrast_correlations);
        json += "}";
    }
    json += "}";

    // Notes
    json += R"(,"notes":[)";
    for (size_t i = 0; i < result.notes.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"" + json_escape(result.notes[i]) + "\"";
    }
    json += "]}";

    return json;
}

std::string CaptureCoordinator::serialize_comparison(const ComparisonResult& result) {
    std::string json = R"({"ok":true,"schema_version":1,"mode":")" +
        std::string(analysis_mode_str(result.mode)) + "\"";

    json += R"(,"deltas":[)";
    for (size_t i = 0; i < result.deltas.size(); ++i) {
        if (i > 0) json += ",";
        json += R"({"label":")" + json_escape(result.deltas[i].label) + "\"";
        json += R"(,"magnitude":)" + float_str(result.deltas[i].magnitude) + "}";
    }
    json += "]";

    json += R"(,"notes":[)";
    for (size_t i = 0; i < result.notes.size(); ++i) {
        if (i > 0) json += ",";
        json += "\"" + json_escape(result.notes[i]) + "\"";
    }
    json += "]}";

    return json;
}

// ---------------------------------------------------------------------------
// tick_analysis — called each frame to advance deferred analysis captures
// ---------------------------------------------------------------------------

void CaptureCoordinator::tick_analysis(WGPUDevice device, WGPUQueue queue,
                                        WGPUTexture capture_tex,
                                        uint32_t tex_width, uint32_t tex_height) {
    // Move newly queued analysis requests into active pending list
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& req : pending_analysis_requests_) {
            PendingAnalysis pa;
            pa.mode = req.mode;
            pa.window_seconds = req.window_seconds;
            pa.include_payload = req.include_payload;
            pa.node_id = req.node_id;
            pa.promise = std::move(req.promise);
            pending_analyses_.push_back(std::move(pa));
        }
        pending_analysis_requests_.clear();
    }

    // Process compare requests immediately (they capture two snapshots right now)
    {
        std::vector<CompareRequest> compares;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            compares.swap(pending_compare_requests_);
        }
        for (auto& creq : compares) {
            try {
                // Capture snapshot A
                AnalysisResult result_a;
                result_a.mode = creq.mode;

                if (creq.mode == AnalysisMode::Audio || creq.mode == AnalysisMode::AV) {
                    if (audio_) {
                        uint64_t sample_count = static_cast<uint64_t>(creq.window_a * AudioEngine::kSampleRate) * 2;
                        uint64_t avail = audio_->available_recorded_samples();
                        if (sample_count > avail) sample_count = avail;
                        if (sample_count > 0) {
                            std::vector<float> samples(sample_count);
                            uint64_t popped = audio_->pop_recorded_samples(samples.data(), sample_count);
                            result_a.audio = analyze_audio(samples.data(), popped, AudioEngine::kSampleRate, 2);
                        }
                    }
                }

                if (creq.mode == AnalysisMode::Frame || creq.mode == AnalysisMode::AV) {
                    if (capture_tex && tex_width > 0 && tex_height > 0) {
                        std::vector<uint8_t> pixels;
                        if (gpu_readback_rgba8(device, queue, capture_tex, tex_width, tex_height, pixels)) {
                            result_a.visual = analyze_frame(pixels.data(), tex_width, tex_height);
                        }
                    }
                }

                // For compare, result_b uses the same snapshot (both captured at same time)
                // In practice, the two captures happen at different times via the API,
                // but for an immediate compare we use the current state for both.
                AnalysisResult result_b = result_a;

                auto comparison = compare_analyses(result_a, result_b);
                creq.promise.set_value(serialize_comparison(comparison));
            } catch (const std::exception& e) {
                creq.promise.set_value(capture_json_err(e.what()));
            } catch (...) {
                creq.promise.set_value(capture_json_err("unknown error during comparison"));
            }
        }
    }

    // Process pending analyses (deferred multi-frame state machine)
    auto now = std::chrono::steady_clock::now();
    // Sample intermediate frames during AV analysis at this cadence (160ms ≈
    // 6 fps). Gives 6 samples for a 1s window, 18+ for 3s — enough for
    // meaningful per-axis correlation without saturating GPU readback.
    constexpr float kAvSampleIntervalSec = 0.16f;
    auto it = pending_analyses_.begin();
    while (it != pending_analyses_.end()) {
        auto& pa = *it;
        try {
            if (!pa.frame_a_captured) {
                // First tick: set solo if needed, capture frame A and start audio tap
                if (!pa.node_id.empty() && runtime_api_) {
                    pa.prev_solo = runtime_api_->solo_node_id();
                    runtime_api_->set_solo(pa.node_id);
                    pa.solo_set = true;
                }
                pa.start_time = now;
                pa.last_sample_time = now;
                pa.frame_a_captured = true;

                if (pa.mode == AnalysisMode::Frame || pa.mode == AnalysisMode::AV) {
                    if (capture_tex && tex_width > 0 && tex_height > 0) {
                        std::vector<uint8_t> pixels;
                        if (gpu_readback_rgba8(device, queue, capture_tex, tex_width, tex_height, pixels)) {
                            pa.frame_w = tex_width;
                            pa.frame_h = tex_height;
                            if (pa.mode == AnalysisMode::AV) {
                                // Seed visual time series with the first sample
                                VisualMetrics vm = analyze_frame(pixels.data(), tex_width, tex_height);
                                VisualSample vs;
                                vs.timestamp_seconds = 0.0f;
                                vs.brightness = vm.mean_brightness;
                                vs.contrast = vm.contrast;
                                vs.motion = 0.0f;
                                pa.visual_samples.push_back(vs);
                                pa.last_frame_pixels = pixels;  // copy for inter-sample motion
                            }
                            pa.frame_a_pixels = std::move(pixels);
                        }
                    }
                }

                if ((pa.mode == AnalysisMode::Audio || pa.mode == AnalysisMode::AV) && audio_) {
                    audio_->start_recording_tap();
                    pa.audio_tap_started = true;
                }

                ++it;
                continue;
            }

            // Check if window has elapsed
            auto elapsed = std::chrono::duration<float>(now - pa.start_time).count();

            // For AV mode, capture intermediate frames during the window
            if (pa.mode == AnalysisMode::AV && elapsed < pa.window_seconds) {
                auto since_last = std::chrono::duration<float>(now - pa.last_sample_time).count();
                if (since_last >= kAvSampleIntervalSec &&
                    capture_tex && tex_width == pa.frame_w && tex_height == pa.frame_h) {
                    std::vector<uint8_t> sample_pixels;
                    if (gpu_readback_rgba8(device, queue, capture_tex, tex_width, tex_height, sample_pixels)) {
                        VisualMetrics vm = analyze_frame(sample_pixels.data(), tex_width, tex_height);
                        VisualSample vs;
                        vs.timestamp_seconds = elapsed;
                        vs.brightness = vm.mean_brightness;
                        vs.contrast = vm.contrast;
                        vs.motion = pa.last_frame_pixels.empty() ? 0.0f
                            : compute_motion(pa.last_frame_pixels.data(), sample_pixels.data(),
                                             tex_width, tex_height);
                        pa.visual_samples.push_back(vs);
                        pa.last_frame_pixels = std::move(sample_pixels);
                        pa.last_sample_time = now;
                    }
                }
                ++it;
                continue;
            }

            if (elapsed < pa.window_seconds) {
                ++it;
                continue;
            }

            // Window elapsed — capture frame B, pop audio, compute metrics
            AnalysisResult result;
            result.mode = pa.mode;

            if (pa.mode == AnalysisMode::Frame || pa.mode == AnalysisMode::AV) {
                if (capture_tex && tex_width > 0 && tex_height > 0) {
                    std::vector<uint8_t> frame_b_pixels;
                    if (gpu_readback_rgba8(device, queue, capture_tex, tex_width, tex_height, frame_b_pixels)) {
                        result.visual = analyze_frame(frame_b_pixels.data(), tex_width, tex_height);
                        // Compute motion between frame A and frame B
                        if (!pa.frame_a_pixels.empty() &&
                            pa.frame_w == tex_width && pa.frame_h == tex_height) {
                            result.visual.motion_magnitude = compute_motion(
                                pa.frame_a_pixels.data(), frame_b_pixels.data(),
                                tex_width, tex_height);

                            if (pa.mode == AnalysisMode::AV) {
                                // Push frame_b as the final visual sample so the
                                // time series spans the full window before correlation.
                                {
                                    VisualSample vs;
                                    vs.timestamp_seconds = elapsed;
                                    vs.brightness = result.visual.mean_brightness;
                                    vs.contrast = result.visual.contrast;
                                    vs.motion = pa.last_frame_pixels.empty() ? 0.0f
                                        : compute_motion(pa.last_frame_pixels.data(), frame_b_pixels.data(),
                                                         tex_width, tex_height);
                                    pa.visual_samples.push_back(vs);
                                }
                                if (pa.audio_tap_started && audio_) {
                                    uint64_t avail = audio_->available_recorded_samples();
                                    std::vector<float> samples;
                                    uint64_t popped = 0;
                                    if (avail > 0) {
                                        samples.resize(avail);
                                        popped = audio_->pop_recorded_samples(samples.data(), avail);
                                        result.audio = analyze_audio(samples.data(), popped, AudioEngine::kSampleRate, 2);
                                    }
                                    // Always call reactivity so visual_samples and window_seconds
                                    // are populated even when audio is silent — caller can then
                                    // distinguish "no audio" from "no analysis ran."
                                    result.av_reactivity = analyze_av_reactivity(
                                        samples.empty() ? nullptr : samples.data(), popped,
                                        AudioEngine::kSampleRate, 2,
                                        pa.visual_samples, pa.window_seconds);
                                }
                            }
                        }
                    }
                }

                if (result.visual.mean_brightness < 0.05f)
                    result.notes.push_back("frame_very_dark");
                if (result.visual.motion_magnitude < 0.01f && pa.mode == AnalysisMode::AV)
                    result.notes.push_back("insufficient_motion");
            }

            if (pa.mode == AnalysisMode::Audio) {
                if (pa.audio_tap_started && audio_) {
                    uint64_t avail = audio_->available_recorded_samples();
                    if (avail > 0) {
                        std::vector<float> samples(avail);
                        uint64_t popped = audio_->pop_recorded_samples(samples.data(), avail);
                        result.audio = analyze_audio(samples.data(), popped, AudioEngine::kSampleRate, 2);
                    }
                }
                if (result.audio.rms < 0.001f)
                    result.notes.push_back("audio_too_quiet");
            }

            if (pa.mode == AnalysisMode::AV && pa.window_seconds < 0.5f)
                result.notes.push_back("window_too_short");

            // Stop audio tap if we started it
            if (pa.audio_tap_started && audio_)
                audio_->stop_recording_tap();

            // Restore solo
            if (pa.solo_set && runtime_api_)
                runtime_api_->set_solo(pa.prev_solo);

            pa.promise.set_value(serialize_analysis(result));
            it = pending_analyses_.erase(it);

        } catch (const std::exception& e) {
            if (pa.audio_tap_started && audio_)
                audio_->stop_recording_tap();
            if (pa.solo_set && runtime_api_)
                runtime_api_->set_solo(pa.prev_solo);
            pa.promise.set_value(capture_json_err(e.what()));
            it = pending_analyses_.erase(it);
        } catch (...) {
            if (pa.audio_tap_started && audio_)
                audio_->stop_recording_tap();
            if (pa.solo_set && runtime_api_)
                runtime_api_->set_solo(pa.prev_solo);
            pa.promise.set_value(capture_json_err("unknown error during analysis"));
            it = pending_analyses_.erase(it);
        }
    }
}

} // namespace vivid
