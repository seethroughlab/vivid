#include "runtime/operator_info_cache.h"
#include "runtime/builtin_operators.h"
#include "runtime/operator_registry.h"
#include "operator_api/types.h"
#include <cstdio>

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
    std::fprintf(stderr, "--- test_operator_info_cache ---\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);

    OperatorInfoCache cache;

    // 1. get() for registered operator returns non-null
    auto info1 = cache.get("audio_out", registry);
    check(info1 != nullptr, "get() for registered operator returns non-null");

    // 2. get() called twice returns same object (cache hit — pointer equality)
    auto info2 = cache.get("audio_out", registry);
    check(info1 == info2, "get() called twice returns same pointer (cache hit)");

    // 3. invalidate(type) causes next get() to return a fresh object
    cache.invalidate("audio_out");
    auto info3 = cache.get("audio_out", registry);
    check(info3 != nullptr, "get() after invalidate() returns non-null");
    check(info3 != info1, "get() after invalidate() returns fresh object");

    // 4. invalidate_all() causes next get() to return fresh objects
    auto v_info1 = cache.get("video_out", registry);
    cache.invalidate_all();
    auto a_info_new = cache.get("audio_out", registry);
    auto v_info_new = cache.get("video_out", registry);
    check(a_info_new != info3, "get() after invalidate_all() returns fresh audio_out");
    check(v_info_new != v_info1, "get() after invalidate_all() returns fresh video_out");

    // 5. get() for unknown type returns null
    auto unknown = cache.get("does_not_exist", registry);
    check(unknown == nullptr, "get() for unknown type returns null");

    // 6. Returned OperatorInfo has correct cadence flags, port count, param count
    if (a_info_new) {
        check(a_info_new->cadence_capability == VIVID_CADENCE_AUDIO_ONLY,
              "audio_out info cadence is AUDIO_ONLY");
        check(a_info_new->ports.size() == 1,
              "audio_out info has 1 port");
        check(a_info_new->params.size() == 1,
              "audio_out info has 1 param");
        check(!a_info_new->params.empty() && a_info_new->params[0].name == "device",
              "audio_out info param[0] name is 'device'");
    }
    if (v_info_new) {
        check(v_info_new->is_gpu,
              "video_out info is_gpu");
        check(v_info_new->ports.size() == 1,
              "video_out info has 1 port");
        check(v_info_new->params.size() == 3,
              "video_out info has 3 params");
        check(!v_info_new->params.empty() && v_info_new->params[0].name == "fit_mode",
              "video_out info param[0] name is 'fit_mode'");
    }

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
