#include "ui/inspector/inspector_controller.h"
#include "ui/graph/node_graph.h"

namespace vivid::ui {

void InspectorController::capture_frame_events(const MouseState& mouse) {
    insp_mouse_left_clicked = mouse.left_clicked;
    insp_mouse_left_released = mouse.left_released;
    insp_mouse_right_clicked = mouse.right_clicked;
}

} // namespace vivid::ui
