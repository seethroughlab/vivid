// Vivid - Entry Point
// Parses command-line arguments and runs the application

#include <vivid/app.h>
#include <vivid/cli.h>
#include <vivid/module_manager.h>
#include <vivid/log.h>
#include <iostream>

int main(int argc, char** argv) {
    // Parse all CLI arguments using CLI11
    auto result = vivid::cli::parseArgs(argc, argv);

    // If a subcommand was handled (help, version, new, bundle, etc.), exit with that code
    if (result.handled) {
        return result.exitCode;
    }

    // If no config returned, something went wrong
    if (!result.config) {
        return 1;
    }

    const auto& config = *result.config;

    vivid::Log::info() << "Vivid - Starting...";

    // Load user-installed modules from ~/.vivid/modules/
    vivid::ModuleManager::instance().loadUserModules();

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
