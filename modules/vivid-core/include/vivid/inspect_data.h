#pragma once

/**
 * @file inspect_data.h
 * @brief Operator introspection data for LLM-driven iteration
 *
 * InspectData provides a key-value bag of metrics and metadata that operators
 * can populate to describe their current state. Used by Chain::inspectAll()
 * and exposed via MCP for autonomous agent evaluation.
 */

#include <string>
#include <unordered_map>
#include <optional>
#include <sstream>
#include <vivid/frame_analysis.h>

namespace vivid {

/**
 * @brief Key-value introspection data from an operator
 *
 * Operators populate this in their inspect() override to report
 * computed metrics (energy, rms, pixel_change_pct) beyond what
 * params() provides.
 */
struct InspectData {
    std::unordered_map<std::string, float> metrics;
    std::unordered_map<std::string, std::string> metadata;
    std::optional<FrameAnalysis> textureAnalysis;

    void set(const std::string& key, float value) {
        metrics[key] = value;
    }

    void set(const std::string& key, const std::string& value) {
        metadata[key] = value;
    }

    std::string toJSON() const {
        std::ostringstream ss;
        ss << "{";

        bool first = true;
        if (!metrics.empty()) {
            ss << "\"metrics\":{";
            for (const auto& [k, v] : metrics) {
                if (!first) ss << ",";
                ss << "\"" << k << "\":" << v;
                first = false;
            }
            ss << "}";
        }

        if (!metadata.empty()) {
            if (!metrics.empty()) ss << ",";
            ss << "\"metadata\":{";
            first = true;
            for (const auto& [k, v] : metadata) {
                if (!first) ss << ",";
                // Escape quotes in value
                std::string escaped = v;
                size_t pos = 0;
                while ((pos = escaped.find('"', pos)) != std::string::npos) {
                    escaped.replace(pos, 1, "\\\"");
                    pos += 2;
                }
                ss << "\"" << k << "\":\"" << escaped << "\"";
                first = false;
            }
            ss << "}";
        }

        if (textureAnalysis.has_value()) {
            if (!metrics.empty() || !metadata.empty()) ss << ",";
            ss << "\"textureAnalysis\":" << textureAnalysis->toJSON();
        }

        ss << "}";
        return ss.str();
    }
};

} // namespace vivid
