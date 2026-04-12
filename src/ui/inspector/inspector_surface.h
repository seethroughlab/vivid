#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vivid::ui {

class InspectorSurface {
public:
    enum class WidgetKind : uint8_t {
        kNone,
        kADSR,
        kStepSeq,
    };

    struct RichTarget {
        WidgetKind kind = WidgetKind::kNone;
        std::string id;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        std::string node_id;

        // ADSR payload.
        std::array<std::string, 4> params{};

        // Step-seq payload.
        uint32_t pi_count = 0;
        uint32_t pi_values = 0;
        uint32_t value_count = 0;
        uint32_t pi_gates = 0;
        uint32_t gate_count = 0;
    };

    void begin_frame() {
        rich_targets_.clear();
    }

    static std::string adsr_id(const std::string& node_id,
                               const std::string& param_a,
                               const std::string& param_d,
                               const std::string& param_s,
                               const std::string& param_r) {
        return node_id + ":adsr:" + param_a + "," + param_d + "," + param_s + "," + param_r;
    }

    static std::string step_seq_id(const std::string& node_id,
                                   uint32_t pi_count,
                                   uint32_t pi_values,
                                   uint32_t value_count,
                                   uint32_t pi_gates,
                                   uint32_t gate_count) {
        return node_id + ":step_seq:" +
               std::to_string(pi_count) + "," +
               std::to_string(pi_values) + "," +
               std::to_string(value_count) + "," +
               std::to_string(pi_gates) + "," +
               std::to_string(gate_count);
    }

    const RichTarget& add_adsr(float x, float y, float w, float h,
                               const std::string& node_id,
                               const std::string& param_a,
                               const std::string& param_d,
                               const std::string& param_s,
                               const std::string& param_r) {
        RichTarget t;
        t.kind = WidgetKind::kADSR;
        t.id = adsr_id(node_id, param_a, param_d, param_s, param_r);
        t.x = x; t.y = y; t.w = w; t.h = h;
        t.node_id = node_id;
        t.params = {param_a, param_d, param_s, param_r};
        rich_targets_.push_back(std::move(t));
        return rich_targets_.back();
    }

    const RichTarget& add_step_seq(float x, float y, float w, float h,
                                   const std::string& node_id,
                                   uint32_t pi_count,
                                   uint32_t pi_values,
                                   uint32_t value_count,
                                   uint32_t pi_gates,
                                   uint32_t gate_count) {
        RichTarget t;
        t.kind = WidgetKind::kStepSeq;
        t.id = step_seq_id(node_id, pi_count, pi_values, value_count, pi_gates, gate_count);
        t.x = x; t.y = y; t.w = w; t.h = h;
        t.node_id = node_id;
        t.pi_count = pi_count;
        t.pi_values = pi_values;
        t.value_count = value_count;
        t.pi_gates = pi_gates;
        t.gate_count = gate_count;
        rich_targets_.push_back(std::move(t));
        return rich_targets_.back();
    }

    const RichTarget* hit_rich_target(float px, float py) const {
        for (const auto& t : rich_targets_) {
            if (px >= t.x && px <= t.x + t.w && py >= t.y && py <= t.y + t.h)
                return &t;
        }
        return nullptr;
    }

    const RichTarget* find_rich_target(const std::string& id) const {
        for (const auto& t : rich_targets_) {
            if (t.id == id) return &t;
        }
        return nullptr;
    }

    void activate(const RichTarget& target, int part) {
        active_id_ = target.id;
        active_kind_ = target.kind;
        active_part_ = part;
    }

    void clear_active() {
        active_id_.clear();
        active_kind_ = WidgetKind::kNone;
        active_part_ = -1;
    }

    bool has_active() const {
        return active_kind_ != WidgetKind::kNone && !active_id_.empty();
    }

    bool is_active(const std::string& id) const {
        return has_active() && active_id_ == id;
    }

    WidgetKind active_kind() const { return active_kind_; }
    int active_part() const { return active_part_; }
    const std::string& active_id() const { return active_id_; }

private:
    std::vector<RichTarget> rich_targets_;
    std::string active_id_;
    WidgetKind active_kind_ = WidgetKind::kNone;
    int active_part_ = -1;
};

} // namespace vivid::ui
