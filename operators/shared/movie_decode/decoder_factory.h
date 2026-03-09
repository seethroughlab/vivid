#pragma once

#include "codec_probe.h"
#include "video_decoder.h"

#include <atomic>
#include <memory>
#include <string>

enum class DecoderBackend {
    AVF,
    HAP
};

struct DecoderRouteDecision {
    DecoderBackend backend = DecoderBackend::AVF;
    bool hap_bc_unavailable_fallback = false;
    bool probe_hap = false;
    bool probe_notchlc = false;
};

DecoderRouteDecision decide_decoder_route(const VideoCodecProbeResult& probe,
                                          bool bc_supported);

struct DecoderLoadResult {
    bool success = false;
    std::unique_ptr<VideoDecoder> decoder;
    std::string diagnostics;
    VideoCodecProbeResult probe;
};

DecoderLoadResult load_video_decoder_for_path(const std::string& path,
                                              bool bc_supported,
                                              const std::atomic<bool>* cancel_flag);
