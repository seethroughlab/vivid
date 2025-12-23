// Vivid - Entry Point
// Parses command-line arguments and runs the application

#include "app.h"
#include <vivid/cli.h>
#include <vivid/addon_manager.h>
#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

// Helper to parse WxH format
static void parseSize(const std::string& s, int& w, int& h) {
    size_t x = s.find('x');
    if (x != std::string::npos) {
        w = std::atoi(s.substr(0, x).c_str());
        h = std::atoi(s.substr(x + 1).c_str());
    }
}

int main(int argc, char** argv) {
    // Handle CLI commands first (vivid new, --help, --version, addons)
    // These don't require GPU initialization
    int cliResult = vivid::cli::handleCommand(argc, argv);
    if (cliResult >= 0) {
        return cliResult;
    }

    std::cout << "Vivid - Starting..." << std::endl;

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
            config.snapshotFrame = std::atoi(argv[++i]);
        } else if (arg.rfind("--snapshot-frame=", 0) == 0) {
            config.snapshotFrame = std::atoi(arg.substr(17).c_str());
        } else if (arg == "--headless") {
            config.headless = true;
        } else if (arg == "--window" && i + 1 < argc) {
            parseSize(argv[++i], config.windowWidth, config.windowHeight);
        } else if (arg.rfind("--window=", 0) == 0) {
            parseSize(arg.substr(9), config.windowWidth, config.windowHeight);
        } else if (arg == "--render" && i + 1 < argc) {
            parseSize(argv[++i], config.renderWidth, config.renderHeight);
        } else if (arg.rfind("--render=", 0) == 0) {
            parseSize(arg.substr(9), config.renderWidth, config.renderHeight);
        } else if (arg == "--fullscreen") {
            config.startFullscreen = true;
        } else if (arg == "--record" && i + 1 < argc) {
            config.recordPath = argv[++i];
        } else if (arg.rfind("--record=", 0) == 0) {
            config.recordPath = arg.substr(9);
        } else if (arg == "--record-fps" && i + 1 < argc) {
            config.recordFps = std::stof(argv[++i]);
        } else if (arg.rfind("--record-fps=", 0) == 0) {
            config.recordFps = std::stof(arg.substr(13));
        } else if (arg == "--record-duration" && i + 1 < argc) {
            config.recordDuration = std::stof(argv[++i]);
        } else if (arg.rfind("--record-duration=", 0) == 0) {
            config.recordDuration = std::stof(arg.substr(18));
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
            config.maxFrames = std::atoi(argv[++i]);
        } else if (arg.rfind("--frames=", 0) == 0) {
            config.maxFrames = std::atoi(arg.substr(9).c_str());
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
            std::cerr << "Warning: --headless without --snapshot, --record, or --frames will run indefinitely.\n";
            std::cerr << "         Use Ctrl+C to stop or add one of these options to capture output.\n";
        }
        std::cout << "Running in headless mode" << std::endl;
    }

    // Create and run application
    vivid::Application app;

    int initResult = app.init(config);
    if (initResult != 0) {
        return initResult;
    }

    return app.run();
}
