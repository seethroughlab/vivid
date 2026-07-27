#ifndef __APPLE__
#include "platform/av_exporter.h"

// Non-macOS: no AVFoundation, so video export is unavailable. start() returns false, which
// VideoRecorder treats as "not recording", so the app still links and runs — the export MCP
// tools / menu action just report failure.
namespace vivid {

namespace {
class NullAVExporter final : public AVExporter {
public:
    bool start(const std::string&, uint32_t, uint32_t, double, uint32_t) override { return false; }
    bool write_video_frame(const uint8_t*, uint32_t, uint32_t) override { return false; }
    bool write_audio_samples(const float*, uint64_t, uint32_t) override { return false; }
    bool finish() override { return false; }
    bool is_recording() const override { return false; }
    const std::string& output_path() const override { static const std::string e; return e; }
    uint64_t frame_count() const override { return 0; }
    double fps() const override { return 60.0; }
    double elapsed_sec() const override { return 0.0; }
};
}  // namespace

std::unique_ptr<AVExporter> make_platform_av_exporter() {
    return std::make_unique<NullAVExporter>();
}

}  // namespace vivid
#endif
