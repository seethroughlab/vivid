// Vivid - Entry Point
// Parses command-line arguments and runs the application

#include "app.h"
#include <vivid/cli.h>
#include <vivid/addon_manager.h>
#include <vivid/log.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>

namespace fs = std::filesystem;

// Safe integer parsing with validation
static bool tryParseInt(const std::string& s, int& out, int minVal = INT_MIN, int maxVal = INT_MAX) {
    try {
        size_t pos;
        int val = std::stoi(s, &pos);
        if (pos != s.length()) return false;  // Trailing characters
        if (val < minVal || val > maxVal) return false;
        out = val;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Safe float parsing with validation
static bool tryParseFloat(const std::string& s, float& out, float minVal = -1e30f, float maxVal = 1e30f) {
    try {
        size_t pos;
        float val = std::stof(s, &pos);
        if (pos != s.length()) return false;  // Trailing characters
        if (val < minVal || val > maxVal) return false;
        out = val;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Helper to parse WxH format with validation
static bool parseSize(const std::string& s, int& w, int& h) {
    size_t x = s.find('x');
    if (x == std::string::npos) {
        std::cerr << "Error: Invalid size format '" << s << "'. Expected WxH (e.g., 1920x1080)\n";
        return false;
    }
    if (!tryParseInt(s.substr(0, x), w, 1, 16384) ||
        !tryParseInt(s.substr(x + 1), h, 1, 16384)) {
        std::cerr << "Error: Invalid size '" << s << "'. Width and height must be 1-16384\n";
        return false;
    }
    return true;
}

// Parse frame specification: "5", "0,5,10", "0-11", "0-20:2"
// Returns true on success, populates frames set
static bool parseFrameSpec(const std::string& spec, std::set<int>& frames) {
    frames.clear();

    // Split by comma first
    size_t start = 0;
    while (start < spec.length()) {
        size_t comma = spec.find(',', start);
        std::string part = (comma == std::string::npos)
            ? spec.substr(start)
            : spec.substr(start, comma - start);

        // Check for range syntax: "start-end" or "start-end:step"
        size_t dash = part.find('-');
        if (dash != std::string::npos && dash > 0) {
            // Range syntax
            int rangeStart, rangeEnd, step = 1;

            if (!tryParseInt(part.substr(0, dash), rangeStart, 0, 1000000)) {
                std::cerr << "Error: Invalid range start in '" << part << "'\n";
                return false;
            }

            std::string rest = part.substr(dash + 1);
            size_t colon = rest.find(':');
            if (colon != std::string::npos) {
                // Has step
                if (!tryParseInt(rest.substr(0, colon), rangeEnd, 0, 1000000) ||
                    !tryParseInt(rest.substr(colon + 1), step, 1, 10000)) {
                    std::cerr << "Error: Invalid range format in '" << part << "'\n";
                    return false;
                }
            } else {
                if (!tryParseInt(rest, rangeEnd, 0, 1000000)) {
                    std::cerr << "Error: Invalid range end in '" << part << "'\n";
                    return false;
                }
            }

            if (rangeEnd < rangeStart) {
                std::cerr << "Error: Range end must be >= start in '" << part << "'\n";
                return false;
            }

            for (int i = rangeStart; i <= rangeEnd; i += step) {
                frames.insert(i);
            }
        } else {
            // Single frame
            int frame;
            if (!tryParseInt(part, frame, 0, 1000000)) {
                std::cerr << "Error: Invalid frame number '" << part << "'\n";
                return false;
            }
            frames.insert(frame);
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    if (frames.empty()) {
        std::cerr << "Error: No valid frames in specification '" << spec << "'\n";
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    // Handle CLI commands first (vivid new, --help, --version, addons)
    // These don't require GPU initialization
    int cliResult = vivid::cli::handleCommand(argc, argv);
    if (cliResult >= 0) {
        return cliResult;
    }

    vivid::Log::info() << "Vivid - Starting...";

    // Load user-installed addons from ~/.vivid/addons/
    vivid::AddonManager::instance().loadUserAddons();

    // Parse arguments into AppConfig
    vivid::AppConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--snapshot" && i + 1 < argc) {
            config.snapshotPath = argv[++i];
        } else if (arg.rfind("--snapshot=", 0) == 0) {
            config.snapshotPath = arg.substr(11);
        } else if (arg == "--snapshot-frame" && i + 1 < argc) {
            if (!parseFrameSpec(argv[++i], config.snapshotFrames)) {
                std::cerr << "  Usage: --snapshot-frame 5 | 0,5,10 | 0-11 | 0-20:2\n";
                return 1;
            }
        } else if (arg.rfind("--snapshot-frame=", 0) == 0) {
            if (!parseFrameSpec(arg.substr(17), config.snapshotFrames)) {
                std::cerr << "  Usage: --snapshot-frame=5 | 0,5,10 | 0-11 | 0-20:2\n";
                return 1;
            }
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--window" && i + 1 < argc) {
            if (!parseSize(argv[++i], config.windowWidth, config.windowHeight)) {
                return 1;
            }
        } else if (arg.rfind("--window=", 0) == 0) {
            if (!parseSize(arg.substr(9), config.windowWidth, config.windowHeight)) {
                return 1;
            }
        } else if (arg == "--render" && i + 1 < argc) {
            if (!parseSize(argv[++i], config.renderWidth, config.renderHeight)) {
                return 1;
            }
        } else if (arg.rfind("--render=", 0) == 0) {
            if (!parseSize(arg.substr(9), config.renderWidth, config.renderHeight)) {
                return 1;
            }
        } else if (arg == "--fullscreen") {
            config.startFullscreen = true;
        } else if (arg == "--record" && i + 1 < argc) {
            config.recordPath = argv[++i];
        } else if (arg.rfind("--record=", 0) == 0) {
            config.recordPath = arg.substr(9);
        } else if (arg == "--record-fps" && i + 1 < argc) {
            if (!tryParseFloat(argv[++i], config.recordFps, 0.1f, 240.0f)) {
                std::cerr << "Error: --record-fps must be a number between 0.1 and 240\n";
                return 1;
            }
        } else if (arg.rfind("--record-fps=", 0) == 0) {
            if (!tryParseFloat(arg.substr(13), config.recordFps, 0.1f, 240.0f)) {
                std::cerr << "Error: --record-fps must be a number between 0.1 and 240\n";
                return 1;
            }
        } else if (arg == "--record-duration" && i + 1 < argc) {
            if (!tryParseFloat(argv[++i], config.recordDuration, 0.01f, 86400.0f)) {
                std::cerr << "Error: --record-duration must be a number between 0.01 and 86400 seconds\n";
                return 1;
            }
        } else if (arg.rfind("--record-duration=", 0) == 0) {
            if (!tryParseFloat(arg.substr(18), config.recordDuration, 0.01f, 86400.0f)) {
                std::cerr << "Error: --record-duration must be a number between 0.01 and 86400 seconds\n";
                return 1;
            }
        } else if (arg == "--record-audio") {
            config.recordAudio = true;
        } else if (arg == "--record-codec" && i + 1 < argc) {
            std::string codec = argv[++i];
            if (codec == "h265" || codec == "hevc") config.recordCodec = vivid::ExportCodec::H265;
            else if (codec == "prores" || codec == "animation") config.recordCodec = vivid::ExportCodec::Animation;
            else config.recordCodec = vivid::ExportCodec::H264;
        } else if (arg.rfind("--record-codec=", 0) == 0) {
            std::string codec = arg.substr(15);
            if (codec == "h265" || codec == "hevc") config.recordCodec = vivid::ExportCodec::H265;
            else if (codec == "prores" || codec == "animation") config.recordCodec = vivid::ExportCodec::Animation;
            else config.recordCodec = vivid::ExportCodec::H264;
        } else if (arg == "--frames" && i + 1 < argc) {
            if (!tryParseInt(argv[++i], config.maxFrames, 1, 10000000)) {
                std::cerr << "Error: --frames must be a positive integer (1-10000000)\n";
                return 1;
            }
        } else if (arg.rfind("--frames=", 0) == 0) {
            if (!tryParseInt(arg.substr(9), config.maxFrames, 1, 10000000)) {
                std::cerr << "Error: --frames must be a positive integer (1-10000000)\n";
                return 1;
            }
        } else if (arg == "--show-ui") {
            config.showUI = true;
        } else if (arg[0] != '-') {
            // Non-flag argument is the project path
            config.projectPath = arg;
        }
    }

    // Headless mode validation
    if (config.headless) {
        if (config.snapshotPath.empty() && config.recordPath.empty() && config.maxFrames == 0) {
            vivid::Log::warn() << "--headless without --snapshot, --record, or --frames will run indefinitely. Use Ctrl+C to stop.";
        }
        vivid::Log::info() << "Running in headless mode";
    }

    // Create and run application
    vivid::Application app;

    int initResult = app.init(config);
    if (initResult != 0) {
        return initResult;
    }

    return app.run();
}
