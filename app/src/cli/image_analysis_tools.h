#pragma once
// ADR-0024 Phase 6 (visual perception): CPU-side analysis of a captured RGBA8 frame + a PNG writer.
// Pure/headless — no GPU, no wgpu. The GPU readback (VisualGraph::read_output_pixels) hands these
// tightly-packed RGBA8 pixels; these turn them into structured perception + a viewable file.
#include "cli/control_handlers.h"   // nlohmann::json alias

#include <cstdint>
#include <string>
#include <vector>

namespace vivid {

using nlohmann::json;

// Structured perception of one RGBA8 frame (row-major, top-left origin, 4 bytes/px). Returns
// {width, height, is_blank, blank_reason, brightness, contrast, activity, dominant_colors[],
//  color_spread, hash}. `hash` is a 64-bit average-hash (16 hex chars) for cheap frame comparison.
json analyze_rgba(const uint8_t* rgba, uint32_t w, uint32_t h);

// Compare two average-hashes (16 hex chars each) → Hamming distance in [0,64] (0 = identical).
int  hash_hamming(const std::string& a, const std::string& b);

// Encode RGBA8 pixels to a PNG (color type 6, 8-bit) byte stream using zlib. Returns false on encode
// error. For in-memory payloads (e.g. a montage sent to a multimodal model) that never touch disk.
bool encode_png(const uint8_t* rgba, uint32_t w, uint32_t h, std::vector<uint8_t>& out);

// Write RGBA8 pixels to a PNG file (color type 6, 8-bit) using zlib. Returns false on any I/O/encode error.
bool write_png(const std::string& path, const uint8_t* rgba, uint32_t w, uint32_t h);

// Decode an image file (PNG/JPG/… via stb_image) to tightly-packed RGBA8. Returns false on error.
// For compare_frames' before/after workflow over saved captures.
bool load_image(const std::string& path, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);

}  // namespace vivid
