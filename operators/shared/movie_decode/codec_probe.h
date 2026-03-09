#pragma once

#include <cstdint>
#include <string>

struct VideoCodecProbeResult {
    bool ok = false;
    uint32_t fourcc = 0;
    std::string fourcc_text;
    bool is_hap = false;
    bool is_notchlc = false;
};

VideoCodecProbeResult probe_video_codec_fourcc(const std::string& path);
