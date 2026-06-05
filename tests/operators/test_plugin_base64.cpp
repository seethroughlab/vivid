// Test: shared plugin base64 (audit 09-F1).
//
// The VST3/CLAP/AU plugin hosts previously triplicated base64 encode/decode
// byte-for-byte; this verifies the extracted canonical implementation
// (operators/shared/plugin_common/base64.h) round-trips exactly for every length
// and matches known RFC 4648 vectors — including the cases the old triplicated
// decode table got WRONG ('=' mapped to 0 instead of skip, which appended
// spurious trailing zero bytes on non-3-aligned input).

#include "shared/plugin_common/base64.h"
#include <cstdint>
#include <string>
#include <vector>
#include "test_helpers.h"

using vivid::plugin_common::base64_encode;
using vivid::plugin_common::base64_decode;

static std::vector<uint8_t> bytes(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

int main() {
    std::fprintf(stderr, "\n=== test_plugin_base64 ===\n\n");

    // Known RFC 4648 vectors (the classic "Man"/"Ma"/"M" padding cases).
    check(base64_encode(bytes("Man").data(), 3) == "TWFu", "encode 'Man' = TWFu");
    check(base64_encode(bytes("Ma").data(), 2) == "TWE=", "encode 'Ma' = TWE=");
    check(base64_encode(bytes("M").data(), 1) == "TQ==", "encode 'M' = TQ==");
    check(base64_encode(nullptr, 0).empty(), "encode empty = ''");

    // Decode of padded input must NOT append spurious trailing zeros (the bug in
    // the old triplicated tables, which mapped '=' to 0 instead of skipping it).
    check(base64_decode("TQ==") == bytes("M"), "decode 'TQ==' = 'M' (no trailing zeros)");
    check(base64_decode("TWE=") == bytes("Ma"), "decode 'TWE=' = 'Ma'");
    check(base64_decode("TWFu") == bytes("Man"), "decode 'TWFu' = 'Man'");
    check(base64_decode("").empty(), "decode '' = empty");
    // Whitespace / stray chars are skipped.
    check(base64_decode("TW Fu") == bytes("Man"), "decode skips embedded whitespace");

    // Exhaustive round-trip across every length 0..64 with a varied byte pattern,
    // covering all three len%3 padding cases.
    {
        std::vector<uint8_t> buf;
        bool all_ok = true;
        for (int len = 0; len <= 64; ++len) {
            buf.clear();
            for (int i = 0; i < len; ++i)
                buf.push_back(static_cast<uint8_t>((i * 37 + 11) & 0xFF));
            std::string enc = base64_encode(buf.data(), buf.size());
            std::vector<uint8_t> dec = base64_decode(enc);
            if (dec != buf) { all_ok = false;
                std::fprintf(stderr, "  round-trip FAILED at len=%d\n", len); }
        }
        check(all_ok, "encode->decode round-trips exactly for every length 0..64");
    }

    // Full-byte-range payload (all 256 values) round-trips.
    {
        std::vector<uint8_t> buf;
        for (int i = 0; i < 256; ++i) buf.push_back(static_cast<uint8_t>(i));
        check(base64_decode(base64_encode(buf.data(), buf.size())) == buf,
              "full 0..255 byte range round-trips");
    }

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
