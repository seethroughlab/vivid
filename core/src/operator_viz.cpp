// Vivid Operator Visualization Registry Implementation

#include <vivid/operator_viz.h>
#include <vivid/operator.h>
#include <typeindex>

namespace vivid {

OperatorVizRegistry& OperatorVizRegistry::instance() {
    static OperatorVizRegistry s_instance;
    return s_instance;
}

bool OperatorVizRegistry::hasVisualizer(Operator* op) {
    if (!op) return false;
    auto& viz = instance().m_visualizers;
    return viz.find(std::type_index(typeid(*op))) != viz.end();
}

bool OperatorVizRegistry::draw(Operator* op, const VizContext& ctx) {
    if (!op) return false;

    auto& viz = instance().m_visualizers;
    auto it = viz.find(std::type_index(typeid(*op)));
    if (it != viz.end()) {
        it->second(op, ctx);
        return true;
    }
    return false;
}

} // namespace vivid
