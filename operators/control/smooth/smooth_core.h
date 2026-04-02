#pragma once

#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

// Shared smoothing state and advance logic for SmoothFr / SmoothAu.

struct SmoothCore {
    float current_ = 0.0f;
    bool first_frame_ = true;

    void advance(float target, float dt, float rise_tau, float fall_tau) {
        if (first_frame_) {
            current_ = target;
            first_frame_ = false;
        } else {
            float tau = (target > current_) ? rise_tau : fall_tau;
            if (tau > 0.0001f) {
                float coeff = 1.0f - std::exp(-dt / tau);
                current_ += (target - current_) * coeff;
            } else {
                current_ = target;
            }
        }
    }
};
