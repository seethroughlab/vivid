#pragma once

// Vivid - Main header
// Include this in your chain.cpp

#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/chain.h>
#include <vivid/param.h>
#include <vivid/color.h>
#include <vivid/display.h>

namespace vivid {

// Chain entry points - implemented by user's chain.cpp
using SetupFn = void(*)(Context&);
using UpdateFn = void(*)(Context&);

/**
 * @brief Chain configuration for window and runtime settings
 *
 * Set initial window size and options before window creation.
 * Pass to VIVID_CHAIN as the optional third argument:
 *
 * @code
 * VIVID_CHAIN(setup, update, {
 *     .windowWidth = 1920,
 *     .windowHeight = 1080,
 *     .resizable = false
 * })
 * @endcode
 */
struct ChainConfig {
    int windowWidth = 1280;      ///< Initial window width
    int windowHeight = 720;      ///< Initial window height
    bool resizable = true;       ///< Allow window resizing
    bool fullscreen = false;     ///< Start in fullscreen mode
    DisplayMode displayMode = DisplayMode::Fit;  ///< Display scaling mode
};

// Platform-specific export macro for DLL symbols
#ifdef _WIN32
    #define VIVID_EXPORT __declspec(dllexport)
#else
    #define VIVID_EXPORT
#endif

/**
 * @brief Export chain entry points (basic version)
 *
 * Uses default 1280x720 window settings.
 * @code
 * VIVID_CHAIN(setup, update)
 * @endcode
 */
#define VIVID_CHAIN(setup_fn, update_fn) \
    extern "C" { \
        VIVID_EXPORT void vivid_setup(vivid::Context& ctx) { setup_fn(ctx); } \
        VIVID_EXPORT void vivid_update(vivid::Context& ctx) { update_fn(ctx); } \
    }

/**
 * @brief Export chain entry points with configuration
 *
 * @code
 * VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
 *     .windowWidth = 1920,
 *     .windowHeight = 1080,
 *     .resizable = false
 * }))
 * @endcode
 *
 * Note: The config must be wrapped in parentheses to prevent
 * the preprocessor from splitting on the commas.
 */
#define VIVID_CHAIN_CONFIG(setup_fn, update_fn, config) \
    extern "C" { \
        VIVID_EXPORT void vivid_setup(vivid::Context& ctx) { setup_fn(ctx); } \
        VIVID_EXPORT void vivid_update(vivid::Context& ctx) { update_fn(ctx); } \
        VIVID_EXPORT vivid::ChainConfig vivid_config() { return config; } \
    }

} // namespace vivid
