#include "video_loader.h"
#include <algorithm>
#include <cctype>
#include <iostream>

namespace vivid {

bool VideoLoader::isSupported(const std::string& path) {
    // Get file extension
    size_t dotPos = path.rfind('.');
    if (dotPos == std::string::npos) {
        return false;
    }

    std::string ext = path.substr(dotPos + 1);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Common video formats
    return ext == "mp4" || ext == "mov" || ext == "m4v" ||
           ext == "avi" || ext == "mkv" || ext == "webm" ||
           ext == "wmv" || ext == "flv" || ext == "mxf" ||
           ext == "ts" || ext == "mts" || ext == "m2ts";
}

// Platform-specific factory implementation
// Each platform will define its own version in video_loader_<platform>.cpp
#if defined(__APPLE__)
    // Forward declare macOS implementation
    std::unique_ptr<VideoLoader> createVideoLoaderMacOS();

    std::unique_ptr<VideoLoader> VideoLoader::create() {
        return createVideoLoaderMacOS();
    }
#elif defined(_WIN32)
    // Forward declare Windows implementation
    std::unique_ptr<VideoLoader> createVideoLoaderWindows();

    std::unique_ptr<VideoLoader> VideoLoader::create() {
        return createVideoLoaderWindows();
    }
#else
    // Forward declare Linux/FFmpeg implementation
    std::unique_ptr<VideoLoader> createVideoLoaderLinux();

    std::unique_ptr<VideoLoader> VideoLoader::create() {
        return createVideoLoaderLinux();
    }
#endif

VideoCodecType detectVideoCodec(const std::string& path) {
    // For now, return Unknown - full detection requires reading file headers
    // This will be implemented properly when we add FFmpeg for HAP demuxing
    //
    // HAP detection requires checking:
    // 1. Container is MOV or AVI
    // 2. Video codec fourcc is 'Hap1', 'Hap5', 'HapY', or 'HapM'
    //
    // For now, we'll detect based on the platform loader's probe

    // Quick check: if file doesn't exist or isn't a video, return Unknown
    if (!VideoLoader::isSupported(path)) {
        return VideoCodecType::Unknown;
    }

    // Default to Standard - platform loaders will update this
    // HAP detection will be added in phase 12.2c
    return VideoCodecType::Standard;
}

} // namespace vivid
