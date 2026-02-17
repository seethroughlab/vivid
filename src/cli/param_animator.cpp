// ParamAnimator — Per-parameter ramp animations with easing

#include <vivid/param_animator.h>
#include <vivid/chain.h>
#include <vivid/operator.h>

namespace vivid {

void ParamAnimator::startRamp(const std::string& op, const std::string& param,
                               float from, float to, float duration, EasingCurve easing) {
    // Replace existing ramp on the same op+param (last-write-wins)
    for (auto& r : m_ramps) {
        if (r.op == op && r.param == param) {
            r.from = from;
            r.to = to;
            r.elapsed = 0.0f;
            r.duration = duration;
            r.easing = easing;
            return;
        }
    }

    m_ramps.push_back({op, param, from, to, 0.0f, duration, easing});
}

void ParamAnimator::update(float dt, Chain& chain) {
    for (auto it = m_ramps.begin(); it != m_ramps.end(); ) {
        it->elapsed += dt;

        Operator* op = chain.getByName(it->op);
        if (!op) {
            // Operator gone — remove ramp
            it = m_ramps.erase(it);
            continue;
        }

        if (it->elapsed >= it->duration) {
            // Ramp complete — set final value exactly
            float val[4] = {it->to, 0, 0, 0};
            op->setParam(it->param, val);
            it = m_ramps.erase(it);
        } else {
            // Interpolate with easing
            float t = it->elapsed / it->duration;
            float easedT = it->easing.apply(t);
            float v = it->from + easedT * (it->to - it->from);
            float val[4] = {v, 0, 0, 0};
            op->setParam(it->param, val);
            ++it;
        }
    }
}

} // namespace vivid
