#pragma once

/**
 * @file easing.h
 * @brief Named easing curves for interpolation (CSS-animation-style)
 *
 * Provides quadratic and smoothstep easing for snapshot crossfades,
 * parameter ramps, and playback script events.
 */

#include <string>

namespace vivid {

enum class EasingType { Linear, EaseIn, EaseOut, EaseInOut };

/**
 * @brief Named easing curve that maps t in [0,1] to an eased value in [0,1]
 */
struct EasingCurve {
    EasingType type = EasingType::Linear;

    static EasingCurve linear()    { return {EasingType::Linear}; }
    static EasingCurve easeIn()    { return {EasingType::EaseIn}; }
    static EasingCurve easeOut()   { return {EasingType::EaseOut}; }
    static EasingCurve easeInOut() { return {EasingType::EaseInOut}; }

    /// Parse from string: "linear", "ease-in", "ease-out", "ease-in-out"
    /// Returns Linear for unrecognized strings.
    static EasingCurve fromString(const std::string& name) {
        if (name == "ease-in")      return easeIn();
        if (name == "ease-out")     return easeOut();
        if (name == "ease-in-out")  return easeInOut();
        return linear();
    }

    /// Apply easing to a linear t value in [0,1]
    float apply(float t) const {
        switch (type) {
            case EasingType::EaseIn:
                return t * t;                       // quadratic ease-in
            case EasingType::EaseOut:
                return t * (2.0f - t);              // quadratic ease-out
            case EasingType::EaseInOut:
                return t * t * (3.0f - 2.0f * t);  // smoothstep
            case EasingType::Linear:
            default:
                return t;
        }
    }

    /// Convert to string name
    const char* toString() const {
        switch (type) {
            case EasingType::EaseIn:    return "ease-in";
            case EasingType::EaseOut:   return "ease-out";
            case EasingType::EaseInOut: return "ease-in-out";
            case EasingType::Linear:
            default:                    return "linear";
        }
    }
};

} // namespace vivid
