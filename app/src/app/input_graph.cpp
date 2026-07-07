#include "app/input_internal.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window.h"
#include "ui/layout.h"
#include "ui/node_graph.h"          // NodeGraph::zoom_at
#include "ui/audio_node_graph.h"
#include "gpu/visual_graph.h"

#include <algorithm>
#include <cmath>

namespace vivid::input {

// Scroll-wheel zoom for whichever graph is under the cursor: the visuals node graph (when the
// graph deep-view is revealed, right of the splitter) and/or the audio-graph deep-view (2i, zoom
// around the cursor). Neither "consumes" the scroll (matches the original fall-through order), so
// this returns void and is called last in scroll_callback.
void graph_scroll(Window& win, App& app, double yoff, double mx, double my) {
    // Visuals pane: zoom the node graph around the cursor.
    if (win.show_graph && app.graph && mx >= win.split_x)
        app.graph->zoom_at(mx, my, std::pow(1.12f, static_cast<float>(yoff)));
    // Audio-graph deep view: zoom around the cursor (keeps the point under the cursor fixed).
    if (win.focus.kind == vivid::FocusContext::Kind::AudioGraph && app.session) {
        vivid::ui::AudioNodeGraph ag; ag.set_source(app.session, win.sel_track);
        const vivid::ui::Rect gp = vivid::ui::audio_graph_panel(win.win_w, win.win_h, win.dock_h);
        ag.set_bounds(gp.x, gp.y, gp.x + gp.w, gp.y + gp.h);
        const vivid::ui::Rect gr = ag.graph_region();
        if (mx >= gr.x && mx < gr.x + gr.w && my >= gr.y && my < gr.y + gr.h) {
            const float z0 = win.ag_zoom;
            const float z1 = std::clamp(z0 * std::pow(1.12f, static_cast<float>(yoff)), 0.35f, 4.0f);
            win.ag_pan_x = static_cast<float>(mx) - gr.x - ((static_cast<float>(mx) - gr.x - win.ag_pan_x) / z0) * z1;
            win.ag_pan_y = static_cast<float>(my) - gr.y - ((static_cast<float>(my) - gr.y - win.ag_pan_y) / z0) * z1;
            win.ag_zoom = z1;
        }
    }
}

}  // namespace vivid::input
