#pragma once

#include "ui/graph/graph_snapshot.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vivid::ui {

enum class InspectorWidgetKind : uint8_t {
    kNone,
    kXYPad,
    kXYPadGroup,
    kColor,
    kADSR,
    kLFO,
    kStepSeq,
    kCustom,
};

struct InspectorWidgetRun {
    InspectorWidgetKind kind = InspectorWidgetKind::kNone;
    uint32_t length = 0;
    std::string widget_id;
};

struct InspectorWidgetRegistryEntry {
    const char* widget_id;
    InspectorWidgetKind kind;
    VividDisplayHint legacy_hint;
    uint32_t required_span;
    bool requires_choices = false;
    bool variable_span = false;
};

inline const std::vector<InspectorWidgetRegistryEntry>& inspector_widget_registry_entries() {
    static const std::vector<InspectorWidgetRegistryEntry> entries = {
        {"seethroughlab.vivid.xy_pad", InspectorWidgetKind::kXYPad, VIVID_DISPLAY_XY_PAD, 2},
        {"seethroughlab.vivid.xy_pad_group", InspectorWidgetKind::kXYPadGroup, VIVID_DISPLAY_XY_PAD, 4, false, true},
        {"seethroughlab.vivid.color", InspectorWidgetKind::kColor, VIVID_DISPLAY_COLOR, 3},
        {"seethroughlab.vivid.adsr", InspectorWidgetKind::kADSR, VIVID_DISPLAY_ADSR, 4},
        {"seethroughlab.vivid.lfo", InspectorWidgetKind::kLFO, VIVID_DISPLAY_LFO, 1, true},
        {"seethroughlab.vivid.step_seq", InspectorWidgetKind::kStepSeq, VIVID_DISPLAY_STEP_SEQ, 2, false, true},
    };
    return entries;
}

inline InspectorWidgetRun inspector_widget_run_at(const std::vector<ParamInfo>& params,
                                                  uint32_t start) {
    if (start >= params.size())
        return {};

    const auto& pd = params[start];
    if (!pd.widget_id.empty()) {
        uint32_t span = pd.widget_span;
        if (span == 0 || start + span > params.size())
            return {};
        for (const auto& entry : inspector_widget_registry_entries()) {
            if (pd.widget_id != entry.widget_id)
                continue;
            if (entry.requires_choices && pd.choice_count == 0)
                return {};
            if (entry.variable_span) {
                return span >= entry.required_span
                    ? InspectorWidgetRun{entry.kind, span, pd.widget_id}
                    : InspectorWidgetRun{};
            }
            return span == entry.required_span
                ? InspectorWidgetRun{entry.kind, span, pd.widget_id}
                : InspectorWidgetRun{};
        }
        return InspectorWidgetRun{InspectorWidgetKind::kCustom, span, pd.widget_id};
    }

    auto same_hint_count = [&](VividDisplayHint hint, uint32_t needed) -> bool {
        if (start + needed > params.size())
            return false;
        for (uint32_t i = 0; i < needed; ++i) {
            if (params[start + i].display_hint != hint)
                return false;
        }
        return true;
    };

    for (const auto& entry : inspector_widget_registry_entries()) {
        if (pd.display_hint != entry.legacy_hint)
            continue;
        if (entry.requires_choices && pd.choice_count == 0)
            return {};
        if (entry.variable_span) {
            uint32_t run = 1;
            while (start + run < params.size() &&
                   params[start + run].display_hint == entry.legacy_hint) {
                ++run;
            }
            return run >= entry.required_span
                ? InspectorWidgetRun{entry.kind, run, entry.widget_id}
                : InspectorWidgetRun{};
        }
        return same_hint_count(entry.legacy_hint, entry.required_span)
            ? InspectorWidgetRun{entry.kind, entry.required_span, entry.widget_id}
            : InspectorWidgetRun{};
    }
    return {};
}

} // namespace vivid::ui
