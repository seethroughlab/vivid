// Headless test for the inspector widget classifier (app/src/ui/param_widget.h):
// which widget an operator param maps to from its type / display hint / enum count.
#include "ui/param_widget.h"
#include "test_helpers.h"

using vivid::ui::NodeWidget;
using vivid::ui::node_widget_kind;

int main() {
    // FLOAT + default hint -> slider; FLOAT + knob hint -> knob.
    CHECK(node_widget_kind(VIVID_PARAM_FLOAT, VIVID_DISPLAY_DEFAULT, 0) == NodeWidget::Slider);
    CHECK(node_widget_kind(VIVID_PARAM_FLOAT, VIVID_DISPLAY_KNOB,    0) == NodeWidget::Knob);
    // INT with a range (no choices) -> slider.
    CHECK(node_widget_kind(VIVID_PARAM_INT,   VIVID_DISPLAY_DEFAULT, 0) == NodeWidget::Slider);
    // BOOL -> toggle, regardless of hint.
    CHECK(node_widget_kind(VIVID_PARAM_BOOL,  VIVID_DISPLAY_DEFAULT, 0) == NodeWidget::Toggle);
    CHECK(node_widget_kind(VIVID_PARAM_BOOL,  VIVID_DISPLAY_KNOB,    0) == NodeWidget::Toggle);
    // INT enum (choice_count > 1) -> dropdown, even with a knob hint.
    CHECK(node_widget_kind(VIVID_PARAM_INT,   VIVID_DISPLAY_DEFAULT, 4) == NodeWidget::Enum);
    CHECK(node_widget_kind(VIVID_PARAM_INT,   VIVID_DISPLAY_KNOB,    3) == NodeWidget::Enum);
    // A single "choice" is not an enum.
    CHECK(node_widget_kind(VIVID_PARAM_INT,   VIVID_DISPLAY_DEFAULT, 1) == NodeWidget::Slider);
    return vivid::test::summary("test_param_widget");
}
