#pragma once
/* Operator metadata detection (SFINAE) — extracted from operator.h so the author-facing
   header stays smaller. These vivid::detail traits let the registration macros read a
   compiled operator's OPTIONAL static members (kDisplayName / kKeywords / kSummary / kRole /
   kAudioRole / kMultiplicityBehavior / …), defaulting when absent. No ABI, no runtime state —
   pure compile-time introspection. Included by operator.h; not meant to be included alone. */
#include "operator_api/types.h"
#include <type_traits>
#include <initializer_list>
#include <cstdint>

namespace vivid { namespace detail {
template <typename T, typename = void>
struct has_strategy_independent : std::false_type {};
template <typename T>
struct has_strategy_independent<T, std::void_t<decltype(T::kStrategyIndependent)>> : std::true_type {};

template <typename T>
constexpr bool get_strategy_independent() {
    if constexpr (has_strategy_independent<T>::value)
        return T::kStrategyIndependent;
    else
        return false;
}

// Multiplicity behavior (the value-model authority). If the operator declares a
// static constexpr kMultiplicityBehavior, use it; otherwise default to Map (the
// pass-through behavior — an op that processes each value independently).
template <typename T, typename = void>
struct has_multiplicity_behavior : std::false_type {};
template <typename T>
struct has_multiplicity_behavior<T, std::void_t<decltype(T::kMultiplicityBehavior)>> : std::true_type {};

template <typename T>
constexpr VividMultiplicityBehavior get_multiplicity_behavior() {
    if constexpr (has_multiplicity_behavior<T>::value)
        return T::kMultiplicityBehavior;
    else
        return VIVID_MULTIPLICITY_MAP;
}
template <typename T, typename = void>
struct has_time_dependent : std::false_type {};
template <typename T>
struct has_time_dependent<T, std::void_t<decltype(T::kTimeDependent)>> : std::true_type {};

template <typename T>
constexpr bool get_time_dependent() {
    if constexpr (has_time_dependent<T>::value)
        return T::kTimeDependent;
    else
        return false;
}

// Audio role (v14+). If the operator declares a static constexpr kAudioRole, use it; otherwise
// DEFAULT (classify by ports). Lets an audio op (esp. a loaded dylib) mark itself a generator /
// note-effect / modulator the way built-ins are marked via audio_op_mark_* (see audio_op_runtime).
template <typename T, typename = void>
struct has_audio_role : std::false_type {};
template <typename T>
struct has_audio_role<T, std::void_t<decltype(T::kAudioRole)>> : std::true_type {};

template <typename T>
constexpr VividAudioRole get_audio_role() {
    if constexpr (has_audio_role<T>::value)
        return T::kAudioRole;
    else
        return VIVID_AUDIO_ROLE_DEFAULT;
}

// Operator role (v16+, ADR-0046). If the operator declares a static constexpr kRole, use it;
// otherwise DEFAULT. Lets an op (esp. a loaded dylib) mark itself a RECIPE / source / adapter / …
// the same way kAudioRole marks a generator / note-effect / modulator.
template <typename T, typename = void>
struct has_operator_role : std::false_type {};
template <typename T>
struct has_operator_role<T, std::void_t<decltype(T::kRole)>> : std::true_type {};

template <typename T>
constexpr VividOperatorRole get_operator_role() {
    if constexpr (has_operator_role<T>::value)
        return T::kRole;
    else
        return VIVID_OP_ROLE_DEFAULT;
}

// v3 metadata: display_name, keywords, summary. All optional — operators that
// don't declare these get auto-derived display name and empty keywords/summary.
template <typename T, typename = void>
struct has_display_name : std::false_type {};
template <typename T>
struct has_display_name<T, std::void_t<decltype(T::kDisplayName)>> : std::true_type {};

template <typename T>
constexpr const char* get_display_name() {
    if constexpr (has_display_name<T>::value)
        return T::kDisplayName;
    else
        return nullptr;
}

template <typename T, typename = void>
struct has_keywords : std::false_type {};
template <typename T>
struct has_keywords<T, std::void_t<decltype(T::kKeywords)>> : std::true_type {};

// kKeywords must be std::array<const char*, N> so .data()/.size() are available
// and the pointer storage is stable for the lifetime of the dylib.
template <typename T>
constexpr const char* const* get_keywords_data() {
    if constexpr (has_keywords<T>::value)
        return T::kKeywords.data();
    else
        return nullptr;
}

template <typename T>
constexpr uint32_t get_keywords_count() {
    if constexpr (has_keywords<T>::value)
        return static_cast<uint32_t>(T::kKeywords.size());
    else
        return 0;
}

template <typename T, typename = void>
struct has_summary : std::false_type {};
template <typename T>
struct has_summary<T, std::void_t<decltype(T::kSummary)>> : std::true_type {};

template <typename T>
constexpr const char* get_summary() {
    if constexpr (has_summary<T>::value)
        return T::kSummary;
    else
        return nullptr;
}

struct metadata_string_sink {
    metadata_string_sink& operator=(const char*) { return *this; }
};

struct metadata_keywords_sink {
    metadata_keywords_sink& operator=(std::initializer_list<const char*>) {
        return *this;
    }
};
}} // namespace vivid::detail
