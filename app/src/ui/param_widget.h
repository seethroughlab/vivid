#pragma once
#include "operator_api/types.h"   // VIVID_PARAM_* / VIVID_DISPLAY_* (macros only)

// Which inspector widget renders an operator param, chosen from its declared metadata
// (base type / display hint / enum choice count). Pure + dependency-light so both the
// dock draw and the input hit-test agree, and it's unit-testable headlessly.
namespace vivid::ui {

enum class NodeWidget { Knob, Slider, Toggle, Enum, File };

inline NodeWidget node_widget_kind(int ptype, int display_hint, int choice_count) {
    if (ptype == VIVID_PARAM_FILE)          return NodeWidget::File;   // file path (Image, ...)
    if (ptype == VIVID_PARAM_BOOL)          return NodeWidget::Toggle;
    if (choice_count > 1)                   return NodeWidget::Enum;   // int enum (choice labels)
    if (display_hint == VIVID_DISPLAY_KNOB) return NodeWidget::Knob;
    return NodeWidget::Slider;                                         // FLOAT / INT default
}

}  // namespace vivid::ui
