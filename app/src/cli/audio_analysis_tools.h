#pragma once

#include "cli/control_handlers.h"

#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

using nlohmann::json;

json analyze_pcm(const std::vector<float>& inL, const std::vector<float>& inR, uint32_t sr, int windows = 16);

// ADR-0024 Phase 5: per-band energy spectrum via a bandpass biquad filterbank (real band energy, no
// FFT). `mode` = "octave" (10 bands 31Hz..16kHz), "mel" (24 mel-spaced), or "linear" (equal bands to
// Nyquist). Returns bands[{center_hz, lo_hz, hi_hz, rms, db}] + energy-weighted centroid + summary.
json analyze_spectrum_bands(const std::vector<float>& L, const std::vector<float>& R, uint32_t sr, const std::string& mode);

// ADR-0024 Phase 5: resolve an audio-source SPEC ({path} | {track,scene} | live/master) to PCM +
// a descriptor. Reused by compare_audio / analyze_spectrum so the same source grammar works everywhere.
bool resolve_audio_source(const ControlCtx& c, const json& spec, double fallback_seconds,
                          std::vector<float>& L, std::vector<float>& R, uint32_t& sr, json& source_json, json& e);

bool load_pcm_file(const std::string& path, uint32_t sr_hint, std::vector<float>& L, std::vector<float>& R, uint32_t& sr);
bool copy_live_capture(const ControlCtx& c, const json& b, double fallback_seconds,
                       std::vector<float>& L, std::vector<float>& R, uint32_t& sr,
                       double& requested_seconds, json& source_json, json& e);
std::string lower_copy_audio(std::string s);

}  // namespace vivid
