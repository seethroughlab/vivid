#pragma once

#include <webgpu/webgpu.h>
#include <cstdint>
#include <string>
#include <atomic>
#include "gpu/gpu_util.h"   // kMsaaSamples, to_sv

struct GLFWwindow;

namespace vivid {

struct FrameState {
    WGPUTexture texture = nullptr;          // swap-chain surface texture (present target)
    WGPUTextureView view = nullptr;         // what the app renders into (4x MSAA color)
    WGPUTextureView resolve_view = nullptr; // surface view; MSAA resolves here in end_frame
    WGPUCommandEncoder encoder = nullptr;
};

// One auxiliary swap-chain surface for a secondary OS window (the visuals pop-out, an editor
// float-out, …) plus its own MSAA color target. Shares the primary device/queue/format — the
// methods take those so the struct owns no context. begin/end mirror GpuContext::begin/end_frame.
struct AuxSurface {
    WGPUSurface     surface   = nullptr;
    WGPUTexture     msaa_tex  = nullptr;
    WGPUTextureView msaa_view = nullptr;
    uint32_t w = 0, h = 0, msaa_w = 0, msaa_h = 0;

    bool is_open() const { return surface != nullptr; }
    bool open(WGPUInstance inst, WGPUDevice dev, WGPUTextureFormat fmt, GLFWwindow* win,
              uint32_t width, uint32_t height, const char* label);
    void resize(WGPUDevice dev, WGPUTextureFormat fmt, uint32_t width, uint32_t height);
    void close();
    // Acquire the next surface texture + an MSAA view to render into. False if unavailable.
    bool begin(WGPUDevice dev, WGPUTextureFormat fmt, bool device_lost, FrameState& frame, const char* label);
    // Resolve + present. False (and frees the frame) on submit failure.
    bool end(WGPUDevice dev, WGPUQueue queue, bool device_lost, const FrameState& frame, const char* label);
private:
    void ensure_msaa(WGPUDevice dev, WGPUTextureFormat fmt, uint32_t width, uint32_t height, const char* label);
};

// Present mode for the swap chain, from the VIVID_PRESENT env var (read once):
//   "immediate"/"off"/"0" -> Immediate (vsync OFF — uncaps the frame rate so the true render
//                            ceiling is measurable), anything else / unset -> Fifo (vsync, default).
// Falls back to Fifo if the surface doesn't advertise Immediate.
WGPUPresentMode vivid_present_mode(const WGPUSurfaceCapabilities& caps);

// GPU-side frame timing via timestamp queries (encoder-level writeTimestamp). Answers the question
// CPU wall-clock can't: is a frame GPU-bound or CPU-bound? Non-blocking — each frame's timestamps are
// resolved into a ring of readback buffers and consumed a few frames later, so the frame loop never
// stalls on the GPU. A no-op (enabled()==false, all methods return immediately) when the adapter lacks
// timestamp support. Resolved values are nanoseconds (wgpuCommandEncoderResolveQuerySet normalizes).
// Main-frame only; results are published to vivid::perf for get_perf.
class GpuTimer {
public:
    static constexpr uint32_t kMaxMarks = 8;   // timestamps per frame (kMaxMarks-1 segments max)
    static constexpr uint32_t kRing     = 4;   // frames in flight before a slot is reused

    bool init(WGPUDevice device, WGPUAdapter adapter);   // false (disabled) if unsupported
    bool enabled() const { return enabled_; }
    void shutdown();

    // Per main frame, in order:
    void begin(WGPUCommandEncoder enc);                     // opens a slot, writes mark 0
    void mark(WGPUCommandEncoder enc, const char* label);   // ends the segment named `label`
    void resolve(WGPUCommandEncoder enc);                   // writes the final mark, records resolve+copy
    void after_submit();                                    // maps the readback buffer; must follow a successful submit

    // Which two native features to request at device creation, iff the adapter supports BOTH.
    // Returns the count written into `out` (0 or 2).
    static uint32_t required_features(WGPUAdapter adapter, WGPUFeatureName* out);

private:
    void drain();   // non-blocking poll + consume any completed slots -> publish to perf
    // Writes one GPU timestamp into query `qidx` via a tiny 1x1 render pass (beginningOfPassWriteIndex).
    // Metal samples timestamps at pass boundaries, so this dummy pass timestamps the current point in the
    // command stream (encoder-level writeTimestamp reads back as 0 on this wgpu-native/Metal backend).
    void write_mark(WGPUCommandEncoder enc, uint32_t qidx);

    struct Slot {
        WGPUBuffer  resolve_buf = nullptr;   // QueryResolve | CopySrc
        WGPUBuffer  read_buf    = nullptr;   // MapRead | CopyDst
        uint32_t    nmarks      = 0;
        const char* labels[kMaxMarks]{};     // labels[i] names the segment ending at mark i (labels[0] unused)
        bool        inflight    = false;
        std::atomic<int> mapped{0};          // set in the map callback: 0=pending, 1=ok, -1=fail
    };
    WGPUDevice      device_     = nullptr;
    WGPUQuerySet    qset_       = nullptr;    // kRing*kMaxMarks timestamp slots
    WGPUTexture     dummy_tex_  = nullptr;    // 1x1 render target for the timing passes (cheap)
    WGPUTextureView dummy_view_ = nullptr;
    Slot         slots_[kRing];
    uint32_t     cur_       = 0;             // ring slot recorded this frame
    uint32_t     nmarks_cur_= 0;
    bool         active_    = false;         // begin() opened a slot this frame
    bool         enabled_   = false;
};

class GpuContext {
public:
    GpuContext() = default;
    ~GpuContext();

    // Non-copyable
    GpuContext(const GpuContext&) = delete;
    GpuContext& operator=(const GpuContext&) = delete;

    bool init(GLFWwindow* window, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);
    bool begin_frame(FrameState& frame);
    bool end_frame(const FrameState& frame);
    void discard_frame(const FrameState& frame);
    void shutdown();

    // Insert a GPU-timing mark on the main frame's encoder, ending the segment named `label`
    // (e.g. after the visual-graph render, `label` = "visuals"). No-op unless GPU timing is enabled.
    // `label` must be a stable string literal — the timer stores the pointer, not a copy.
    void gpu_mark(WGPUCommandEncoder encoder, const char* label) { timer_.mark(encoder, label); }

    // Secondary output surface — the pop-out visuals window. Shares this device/queue;
    // the visuals FBO is blitted into it (see VisualGraph::present_to). Thin delegators over
    // aux_popout_ (an AuxSurface). Absent (has_secondary()==false) when closed.
    bool open_secondary(GLFWwindow* window, uint32_t width, uint32_t height);
    void close_secondary();
    void resize_secondary(uint32_t width, uint32_t height);
    bool has_secondary() const { return aux_popout_.is_open(); }
    bool begin_secondary(FrameState& frame);
    bool end_secondary(const FrameState& frame);

    // Editor float-out surface (UI-5) — an independent secondary window for an operator's custom
    // editor, drawn with its own Renderer2D. Same shared device/queue as everything else.
    bool open_editor_surface(GLFWwindow* window, uint32_t width, uint32_t height);
    void close_editor_surface();
    void resize_editor_surface(uint32_t width, uint32_t height);
    bool has_editor_surface() const { return aux_editor_.is_open(); }
    bool begin_editor_surface(FrameState& frame);
    bool end_editor_surface(const FrameState& frame);

    uint32_t sample_count() const { return kMsaaSamples; }

    WGPUInstance instance() const { return instance_; }
    WGPUAdapter adapter() const { return adapter_; }
    WGPUDevice device() const { return device_; }
    WGPUQueue queue() const { return queue_; }
    WGPUTextureFormat surface_format() const { return surface_format_; }
    bool surface_supports_copy_src() const { return surface_copy_src_; }
    bool bc_texture_compression_enabled() const { return bc_texture_compression_enabled_; }
    bool device_lost() const { return device_lost_; }
    const std::string& last_error() const { return last_error_; }
    uint32_t error_count() const { return error_count_.load(std::memory_order_relaxed); }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

private:
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    WGPUQueue queue_ = nullptr;
    WGPUSurface surface_ = nullptr;
    WGPUTextureFormat surface_format_ = WGPUTextureFormat_Undefined;
    bool surface_copy_src_ = false;
    bool bc_texture_compression_enabled_ = false;
    WGPUPresentMode present_mode_ = WGPUPresentMode_Fifo;   // chosen once in init() (VIVID_PRESENT)
    bool device_lost_ = false;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // 4x MSAA color target the whole frame renders into; resolved to the surface
    // in end_frame. Recreated on resize. (void ensure_msaa below.)
    WGPUTexture msaa_tex_ = nullptr;
    WGPUTextureView msaa_view_ = nullptr;
    uint32_t msaa_w_ = 0;
    uint32_t msaa_h_ = 0;
    void ensure_msaa(uint32_t width, uint32_t height);

    // Secondary (pop-out) + editor float-out surfaces. Share device_/queue_/format via AuxSurface.
    AuxSurface aux_popout_;
    AuxSurface aux_editor_;

    // GPU-side frame timing (timestamp queries). Disabled/no-op if the adapter lacks support.
    GpuTimer timer_;

    // Last error captured from the uncaptured error callback (for crash diagnostics)
    std::string last_error_;
    WGPUErrorType last_error_type_ = WGPUErrorType_NoError;
    std::atomic<uint32_t> error_count_{0};   // total uncaptured errors (health signal, P4.3)
};

// Finish, submit, and release a command encoder.  Returns false if the encoder
// was in an error state (null command buffer).  GPU errors are handled by the
// uncaptured error callback configured on the device — error scopes are not
// used because wgpu-native panics if PushErrorScope is called on a lost device.
bool gpu_submit(WGPUDevice device, WGPUQueue queue, WGPUCommandEncoder encoder,
                const char* label = "Commands");

} // namespace vivid
