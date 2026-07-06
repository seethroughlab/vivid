#pragma once
// base64.h — canonical RFC 4648 base64 (standard alphabet, '+' '/' , '=' pad)
// shared by the VST3 / CLAP / AU plugin hosts for plugin-state serialization.
//
// Previously this encode/decode pair (plus a 256-entry decode table) was
// triplicated byte-for-byte across vst3_host_common.h, clap_host_common.h, and
// au_host_common.h (audit 09-F1 / 05-F9). Those copies also shared a decode
// table typo — '=' mapped to 0 instead of being skipped — which appended
// spurious trailing zero bytes when decoding non-3-byte-aligned input. This
// shared implementation skips '=' (and any non-alphabet byte) correctly, so a
// round-trip is exact for any length (see tests/operators/test_plugin_base64.cpp).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vivid::plugin_common {

inline std::string base64_encode(const uint8_t* data, std::size_t len) {
    static const char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (std::size_t i = 0; i < len; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < len) n |= static_cast<uint32_t>(data[i + 2]);
        out.push_back(kTable[(n >> 18) & 0x3F]);
        out.push_back(kTable[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? kTable[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? kTable[ n       & 0x3F] : '=');
    }
    return out;
}

inline std::vector<uint8_t> base64_decode(const std::string& s) {
    auto sextet = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;  // '=' padding, whitespace, or anything else → skip
    };
    std::vector<uint8_t> out;
    out.reserve((s.size() / 4) * 3);
    uint32_t acc = 0;
    int bits = 0;
    for (unsigned char c : s) {
        int v = sextet(c);
        if (v < 0) continue;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>(acc >> bits));
            acc &= (1u << bits) - 1u;
        }
    }
    return out;
}

}  // namespace vivid::plugin_common
