#include "runtime/operators/operator_info_cache.h"
#include "runtime/operators/builtin_operators.h"
#include "runtime/operators/operator_loader.h"
#include "runtime/operators/operator_registry.h"
#include "operator_api/types.h"
#include <cstdio>
#include <vector>
#include "test_helpers.h"

static int32_t s_show_when_mode_2[] = {2};
static int32_t s_hide_when_mode_1_or_2[] = {1, 2};

static VividParamDescriptor s_visibility_params[3];
static VividOperatorDescriptor s_visibility_desc;

static const VividOperatorDescriptor* visibility_descriptor() {
    static bool inited = false;
    if (!inited) {
        inited = true;
        s_visibility_params[0] = {};
        s_visibility_params[0].name = "mode";
        s_visibility_params[0].type = VIVID_PARAM_INT;
        s_visibility_params[0].default_value = 0.0f;
        s_visibility_params[0].min_value = 0.0f;
        s_visibility_params[0].max_value = 2.0f;

        s_visibility_params[1] = {};
        s_visibility_params[1].name = "sync_division";
        s_visibility_params[1].type = VIVID_PARAM_INT;
        s_visibility_params[1].default_value = 0.0f;
        s_visibility_params[1].min_value = 0.0f;
        s_visibility_params[1].max_value = 8.0f;
        s_visibility_params[1].visible_when_param = "mode";
        s_visibility_params[1].visible_when_op = VIVID_PARAM_VIS_EQ;
        s_visibility_params[1].visible_when_values = s_show_when_mode_2;
        s_visibility_params[1].visible_when_value_count = 1;
        s_visibility_params[1].widget_id = "com.example.package.sync_division";
        s_visibility_params[1].widget_span = 1;

        s_visibility_params[2] = {};
        s_visibility_params[2].name = "frequency";
        s_visibility_params[2].type = VIVID_PARAM_FLOAT;
        s_visibility_params[2].default_value = 1.0f;
        s_visibility_params[2].min_value = 0.01f;
        s_visibility_params[2].max_value = 20.0f;
        s_visibility_params[2].visible_when_param = "mode";
        s_visibility_params[2].visible_when_op = VIVID_PARAM_VIS_NE;
        s_visibility_params[2].visible_when_values = s_hide_when_mode_1_or_2;
        s_visibility_params[2].visible_when_value_count = 2;

        s_visibility_desc = {};
        s_visibility_desc.name = "visibility_test";
        s_visibility_desc.param_count = 3;
        s_visibility_desc.params = s_visibility_params;
    }
    return &s_visibility_desc;
}

static void* visibility_create() { return new int(1); }
static void visibility_destroy(void* p) { delete static_cast<int*>(p); }
static void visibility_process(void*, VividFrameContext*) {}

static void test_visibility_eval() {
    using vivid::ui::ParamVisibilityCondition;
    using vivid::ui::param_visibility_matches;

    ParamVisibilityCondition eq;
    eq.param_index = 0;
    eq.op = VIVID_PARAM_VIS_EQ;
    eq.values = {2};
    check(param_visibility_matches(eq, std::vector<float>{2.0f}), "EQ matches controller value");
    check(!param_visibility_matches(eq, std::vector<float>{1.0f}), "EQ rejects different value");

    ParamVisibilityCondition ne;
    ne.param_index = 0;
    ne.op = VIVID_PARAM_VIS_NE;
    ne.values = {1, 2};
    check(param_visibility_matches(ne, std::vector<float>{0.0f}), "NE accepts values outside set");
    check(!param_visibility_matches(ne, std::vector<float>{1.0f}), "NE rejects first hidden value");
    check(!param_visibility_matches(ne, std::vector<float>{2.0f}), "NE rejects second hidden value");

    ParamVisibilityCondition missing_index = eq;
    missing_index.param_index = 12;
    check(param_visibility_matches(missing_index, std::vector<float>{2.0f}), "out-of-bounds controller fails open");

    ParamVisibilityCondition empty_values = eq;
    empty_values.values.clear();
    check(param_visibility_matches(empty_values, std::vector<float>{2.0f}), "empty value list fails open");

    ParamVisibilityCondition always;
    check(param_visibility_matches(always, std::vector<float>{0.0f}), "default condition is visible");
}

int main() {
    std::fprintf(stderr, "--- test_operator_info_cache ---\n");

    vivid::OperatorRegistry registry;
    register_builtin_operators(registry);
    registry.register_builtin("visibility_test", visibility_descriptor, visibility_create,
                              visibility_destroy, visibility_process);

    OperatorInfoCache cache;
    test_visibility_eval();

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

    // 6. Returned OperatorInfo has correct port count, param count
    if (a_info_new) {
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

    auto vis_info = cache.get("visibility_test", registry);
    check(vis_info != nullptr, "visibility_test info returned");
    if (vis_info && vis_info->params.size() == 3) {
        const auto& sync = vis_info->params[1];
        check(sync.visible_when_param == "mode", "visible_when controller name copied");
        check(sync.visible_when_op == VIVID_PARAM_VIS_EQ, "visible_when EQ op copied");
        check(sync.visible_when_values.size() == 1 && sync.visible_when_values[0] == 2,
              "visible_when values copied");
        check(sync.widget_id == "com.example.package.sync_division",
              "widget id copied");
        check(sync.widget_span == 1,
              "widget span copied");
        check(sync.visibility.param_index == 0, "visible_when controller resolved to param index");
        check(sync.visibility.op == VIVID_PARAM_VIS_EQ, "visibility condition op resolved");
        check(sync.visibility.values.size() == 1 && sync.visibility.values[0] == 2,
              "visibility condition values resolved");

        const auto& freq = vis_info->params[2];
        check(freq.visibility.op == VIVID_PARAM_VIS_NE, "NE condition resolved");
        check(freq.visibility.values.size() == 2 &&
              freq.visibility.values[0] == 1 && freq.visibility.values[1] == 2,
              "multi-value NE condition resolved");
    }

    VividParamDescriptor missing_controller = {};
    missing_controller.name = "missing";
    missing_controller.visible_when_param = "does_not_exist";
    missing_controller.visible_when_op = VIVID_PARAM_VIS_EQ;
    missing_controller.visible_when_values = s_show_when_mode_2;
    missing_controller.visible_when_value_count = 1;
    if (vis_info) {
        auto resolved = resolve_param_visibility(missing_controller, vis_info->params);
        check(resolved.op == VIVID_PARAM_VIS_ALWAYS && resolved.param_index < 0,
              "missing controller fails open");
    }

    VividParamDescriptor invalid_op = missing_controller;
    invalid_op.visible_when_param = "mode";
    invalid_op.visible_when_op = 999u;
    if (vis_info) {
        auto resolved = resolve_param_visibility(invalid_op, vis_info->params);
        check(resolved.op == VIVID_PARAM_VIS_ALWAYS && resolved.param_index < 0,
              "invalid visibility op fails open");
    }

    VividParamDescriptor empty_values = missing_controller;
    empty_values.visible_when_param = "mode";
    empty_values.visible_when_values = nullptr;
    empty_values.visible_when_value_count = 0;
    if (vis_info) {
        auto resolved = resolve_param_visibility(empty_values, vis_info->params);
        check(resolved.op == VIVID_PARAM_VIS_ALWAYS && resolved.param_index < 0,
              "empty descriptor values fail open");
    }

    // OperatorInfo::has_editor is populated from the loader's editor symbols.
    // Uses the editor_test_op fixture built via add_vivid_test_fixture. The
    // fixture's descriptor name is "EditorTestOp" per kName.
    {
        vivid::OperatorLoader editor_loader;
        if (editor_loader.load("./editor_test_op.dylib")) {
            auto info = cache.get("EditorTestOp", registry, &editor_loader);
            check(info != nullptr, "EditorTestOp info returned via fallback loader");
            if (info) {
                check(info->has_editor, "OperatorInfo::has_editor true for editor_test_op");
            }
        } else {
            check(false, "editor_test_op.dylib failed to load from cwd");
        }
    }

    // Non-editor operator (any builtin without VIVID_EDITOR) has has_editor == false.
    {
        auto info = cache.get("audio_out", registry);
        check(info != nullptr, "audio_out info returned");
        if (info) {
            check(!info->has_editor, "OperatorInfo::has_editor false for non-editor op");
        }
    }

    // v3 metadata: descriptor with no display_name -> auto-derived; with
    // explicit display_name/keywords/summary -> copied verbatim. Also asserts
    // SearchHaystack is populated.
    {
        std::fprintf(stderr, "\n=== v3 metadata: auto-derive ===\n");
        cache.invalidate_all();
        auto info = cache.get("audio_out", registry);
        check(info != nullptr, "audio_out info returned");
        if (info) {
            check(info->display_name == "Audio Out",
                  "audio_out auto-derives display_name 'Audio Out'");
            check(info->keywords.empty(), "no keywords when descriptor has none");
            check(info->summary.empty(), "no summary when descriptor has none");
            check(info->search.display_name_norm == "audio out",
                  "search.display_name_norm normalized");
            check(info->search.id_norm.find("audio out") != std::string::npos,
                  "search.id_norm contains space-split form");
            check(info->search.id_norm.find("audio_out") == std::string::npos,
                  "search.id_norm strips underscores via normalize_for_search");
        }
    }
    {
        std::fprintf(stderr, "\n=== v3 metadata: explicit fields ===\n");
        static const char* s_kw[] = {"harmony", "diatonic"};
        static VividOperatorDescriptor s_meta_desc{};
        static bool s_inited = false;
        if (!s_inited) {
            s_inited = true;
            s_meta_desc.name = "MetaTest";
            s_meta_desc.display_name = "Custom Display";
            s_meta_desc.keywords = s_kw;
            s_meta_desc.keyword_count = 2;
            s_meta_desc.summary = "A test operator with explicit metadata.";
        }
        registry.register_builtin(
            "MetaTest", []() -> const VividOperatorDescriptor* { return &s_meta_desc; },
            visibility_create, visibility_destroy, visibility_process);
        auto info = cache.get("MetaTest", registry);
        check(info != nullptr, "MetaTest info returned");
        if (info) {
            check(info->display_name == "Custom Display",
                  "explicit display_name preserved");
            check(info->keywords.size() == 2 && info->keywords[0] == "harmony" &&
                      info->keywords[1] == "diatonic",
                  "keywords copied in order");
            check(info->summary == "A test operator with explicit metadata.",
                  "summary copied");
            check(info->search.keyword_norms.size() == 2 &&
                      info->search.keyword_norms[0] == "harmony",
                  "search.keyword_norms populated");
            check(info->search.summary_norm == "a test operator with explicit metadata",
                  "search.summary_norm normalized (trailing period stripped)");
        }
    }

    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
