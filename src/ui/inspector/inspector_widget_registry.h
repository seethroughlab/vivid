#pragma once

#include "ui/graph/graph_snapshot.h"

#include <cstdint>
#include <vector>

namespace vivid::ui {

enum class InspectorWidgetKind : uint8_t {
    kNone,
    kXYPad,
    kColor,
    kADSR,
    kLFO,
    kStepSeq,
};

struct InspectorWidgetRun {
    InspectorWidgetKind kind = InspectorWidgetKind::kNone;
    uint32_t length = 0;
};

inline InspectorWidgetRun inspector_widget_run_at(const std::vector<ParamInfo>& params,
                                                  uint32_t start) {
    if (start >= params.size())
        return {};

    auto same_hint_count = [&](VividDisplayHint hint, uint32_t needed) -> bool {
        if (start + needed > params.size())
            return false;
        for (uint32_t i = 0; i < needed; ++i) {
            if (params[start + i].display_hint != hint)
                return false;
        }
        return true;
    };

    const auto& pd = params[start];
    switch (pd.display_hint) {
    case VIVID_DISPLAY_XY_PAD:
        return same_hint_count(VIVID_DISPLAY_XY_PAD, 2)
            ? InspectorWidgetRun{InspectorWidgetKind::kXYPad, 2}
            : InspectorWidgetRun{};
    case VIVID_DISPLAY_COLOR:
        return same_hint_count(VIVID_DISPLAY_COLOR, 3)
            ? InspectorWidgetRun{InspectorWidgetKind::kColor, 3}
            : InspectorWidgetRun{};
    case VIVID_DISPLAY_ADSR:
        return same_hint_count(VIVID_DISPLAY_ADSR, 4)
            ? InspectorWidgetRun{InspectorWidgetKind::kADSR, 4}
            : InspectorWidgetRun{};
    case VIVID_DISPLAY_LFO:
        return pd.choice_count > 0
            ? InspectorWidgetRun{InspectorWidgetKind::kLFO, 1}
            : InspectorWidgetRun{};
    case VIVID_DISPLAY_STEP_SEQ: {
        uint32_t run = 1;
        while (start + run < params.size() &&
               params[start + run].display_hint == VIVID_DISPLAY_STEP_SEQ) {
            ++run;
        }
        return run >= 2
            ? InspectorWidgetRun{InspectorWidgetKind::kStepSeq, run}
            : InspectorWidgetRun{};
    }
    default:
        return {};
    }
}

} // namespace vivid::ui
