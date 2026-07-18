#pragma once

#include "cli/control_handlers.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

using nlohmann::json;

json analyze_pcm(const std::vector<float>& inL, const std::vector<float>& inR, uint32_t sr, int windows = 16);
bool load_pcm_file(const std::string& path, uint32_t sr_hint, std::vector<float>& L, std::vector<float>& R, uint32_t& sr);
bool copy_live_capture(const ControlCtx& c, const json& b, double fallback_seconds,
                       std::vector<float>& L, std::vector<float>& R, uint32_t& sr,
                       double& requested_seconds, json& source_json, json& e);
std::string lower_copy_audio(std::string s);

}  // namespace vivid
