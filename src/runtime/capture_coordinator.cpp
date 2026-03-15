#include "runtime/capture_coordinator.h"
#include "runtime/av_exporter.h"
#include "runtime/audio_engine.h"
#include "common/gpu_util.h"
#include <webgpu/wgpu.h>
#include <stb_image_write.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cstdlib>

namespace vivid {

CaptureCoordinator::CaptureCoordinator() : exporter_(std::make_unique<AVExporter>()) {}
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

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = to_sv("Capture Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    // Wait for GPU work
    {
        bool work_done = false;
        WGPUQueueWorkDoneCallbackInfo work_cb{};
        work_cb.mode = WGPUCallbackMode_AllowSpontaneous;
        work_cb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
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
    return !pending_.empty();
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

} // namespace vivid
