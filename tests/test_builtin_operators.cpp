#include "runtime/builtin_operators.h"
#include "runtime/operator_registry.h"
#include "operator_api/types.h"
#include <cstdio>
#include <cstring>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

int main() {
    std::fprintf(stderr, "--- test_builtin_operators ---\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);

    // 1. audio_out is registered
    auto* audio_loader = registry.find_loaded("audio_out");
    check(audio_loader != nullptr, "audio_out is registered in the registry");

    // 2. video_out is registered
    auto* video_loader = registry.find_loaded("video_out");
    check(video_loader != nullptr, "video_out is registered in the registry");

    // 3. audio_out descriptor: domain = AUDIO, 1 input port (multi-channel), 1 param
    if (audio_loader) {
        const auto* desc = audio_loader->descriptor();
        check(desc != nullptr, "audio_out descriptor is non-null");
        if (desc) {
            check(desc->domain == VIVID_DOMAIN_AUDIO,
                  "audio_out domain is VIVID_DOMAIN_AUDIO");
            check(desc->port_count == 1,
                  "audio_out has 1 port (input)");
            if (desc->port_count >= 1) {
                check(std::strcmp(desc->ports[0].name, "input") == 0,
                      "audio_out port[0] is 'input'");
            }
            check(desc->param_count == 1, "audio_out has 1 param (device)");
            if (desc->param_count >= 1) {
                check(std::strcmp(desc->params[0].name, "device") == 0,
                      "audio_out param[0] is 'device'");
            }
            check(desc->time_dependent == 1, "audio_out is time_dependent");
        }
    }

    // 4. video_out descriptor: domain = GPU, 1 input port, 1 param
    if (video_loader) {
        const auto* desc = video_loader->descriptor();
        check(desc != nullptr, "video_out descriptor is non-null");
        if (desc) {
            check(desc->domain == VIVID_DOMAIN_GPU,
                  "video_out domain is VIVID_DOMAIN_GPU");
            check(desc->port_count == 1, "video_out has 1 port (input)");
            if (desc->port_count >= 1) {
                check(std::strcmp(desc->ports[0].name, "input") == 0,
                      "video_out port[0] is 'input'");
            }
            check(desc->param_count == 3, "video_out has 3 params (fit_mode, fullscreen, display_target)");
            if (desc->param_count >= 1) {
                check(std::strcmp(desc->params[0].name, "fit_mode") == 0,
                      "video_out param[0] is 'fit_mode'");
            }
            check(desc->time_dependent == 1, "video_out is time_dependent");
        }
    }

    // 5. Calling register_builtin_operators() twice doesn't crash
    register_builtin_operators(registry);
    check(true, "register_builtin_operators() twice doesn't crash");

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
