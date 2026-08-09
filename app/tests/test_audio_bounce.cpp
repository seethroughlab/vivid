// ADR-0032: headless coverage of the offline master-mix WAV bounce (bounce_session_to_wav). Builds a
// real native-op Session (deterministic, no plugins/GUI/audio device — same posture as
// test_session_executor), bounces it to a .wav, and asserts the file is a valid WAV with the expected
// frame count, that audio was rendered, that two fresh sessions bounce byte-identically (determinism),
// that the clip flag tracks the peak, and that bad requests are rejected.
//
// macOS-only: the audio engine reaches CoreFoundation via the VST3 host, so this lives in the full-app
// configure (VIVID_BUILD_APP=ON), not the portable Linux tests tier.
#include "audio/audio_bounce.h"
#include "audio/vst3_host.h"
#include "audio/builtin_audio_ops.h"   // register_builtin_audio_ops
#include "gpu/op_runtime.h"            // vivid::OpRegistry
#include "midi/midi_clip.h"            // ClipNote
#include "transport.h"
#include "test_helpers.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace vivid::session;

static Session* make_tone_session(vivid::OpRegistry& reg, uint32_t sr) {
    Session* s = session_create(sr);
    session_set_op_registry(s, &reg);
    const int t = session_add_graph_track(s, "T");
    session_set_track_audio_instrument(s, t, "TestTone");
    ClipNote n{}; n.pitch = 60; n.start = 0.0; n.dur = 100.0; n.vel = 0.9f;
    session_set_clip(s, t, 0, &n, 1, 100.0);
    session_launch_scene(s, 0);
    return s;
}

// Minimal WAV parser: enough to validate the header ma_encoder wrote.
struct WavInfo {
    bool     ok = false;
    uint16_t format = 0;       // 1 = PCM, 3 = IEEE float, 0xFFFE = extensible
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits = 0;
    uint64_t data_bytes = 0;
};

static uint32_t rd_u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
static uint16_t rd_u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

static WavInfo read_wav(const std::string& path, std::vector<uint8_t>* data_out = nullptr) {
    WavInfo w;
    std::ifstream f(path, std::ios::binary);
    if (!f) return w;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < 44) return w;
    if (std::string(buf.begin(), buf.begin() + 4) != "RIFF") return w;
    if (std::string(buf.begin() + 8, buf.begin() + 12) != "WAVE") return w;
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const std::string id(buf.begin() + pos, buf.begin() + pos + 4);
        const uint32_t sz = rd_u32(&buf[pos + 4]);
        const size_t body = pos + 8;
        if (id == "fmt " && body + 16 <= buf.size()) {
            w.format      = rd_u16(&buf[body + 0]);
            w.channels    = rd_u16(&buf[body + 2]);
            w.sample_rate = rd_u32(&buf[body + 4]);
            w.bits        = rd_u16(&buf[body + 14]);
        } else if (id == "data") {
            w.data_bytes = sz;
            if (data_out && body + sz <= buf.size())
                data_out->assign(buf.begin() + body, buf.begin() + body + sz);
            w.ok = (w.channels != 0);
            break;
        }
        pos = body + sz + (sz & 1);   // chunks are word-aligned
    }
    return w;
}

int main() {
    vivid::OpRegistry reg;
    vivid::register_builtin_audio_ops(reg);
    const uint32_t sr = 48000;

    Transport tr;
    tr.configure_capture(sr);            // sets audio_sample_rate() the bounce reads
    tr.bpm.store(120.0);

    const auto tmp = std::filesystem::temp_directory_path();
    const std::string p1 = (tmp / "vivid_bounce_a.wav").string();
    const std::string p2 = (tmp / "vivid_bounce_b.wav").string();

    // --- 1. A native tone session bounces to a valid WAV of the requested length, with audio in it. ---
    {
        Session* s = make_tone_session(reg, sr);
        vivid::BounceRequest req; req.path = p1; req.seconds = 1.0;
        vivid::BounceResult res; std::string err;
        const bool ok = vivid::bounce_session_to_wav(s, tr, req, res, &err);
        CHECK(ok);
        CHECK(res.frames == static_cast<uint64_t>(std::ceil(1.0 * sr)));   // 1 s at 48k
        CHECK(res.peak > 0.01f);                                          // the note reached the output
        CHECK(res.clipped == (res.peak > 1.0f));                          // flag tracks the peak
        CHECK(res.peak < 1.0f && !res.clipped);                          // a single tone shouldn't clip

        std::vector<uint8_t> d1;
        WavInfo w = read_wav(p1, &d1);
        CHECK(w.ok);
        CHECK(w.channels == 2);
        CHECK(w.sample_rate == sr);
        CHECK(w.bits == 32);
        CHECK(w.format == 3 || w.format == 0xFFFE);                       // IEEE float (dr_wav) or extensible
        CHECK(w.data_bytes == res.frames * 2u * 4u);                      // frames * 2ch * 4 bytes (f32)
        CHECK(d1.size() == w.data_bytes);
        session_destroy(s);
    }

    // --- 2. Determinism: two FRESH identical sessions bounce byte-identical audio data. ---
    {
        Session* a = make_tone_session(reg, sr);
        Session* b = make_tone_session(reg, sr);
        vivid::BounceResult ra, rb; std::string err;
        vivid::BounceRequest reqa; reqa.path = p1; reqa.seconds = 0.5;
        vivid::BounceRequest reqb; reqb.path = p2; reqb.seconds = 0.5;
        CHECK(vivid::bounce_session_to_wav(a, tr, reqa, ra, &err));
        CHECK(vivid::bounce_session_to_wav(b, tr, reqb, rb, &err));
        CHECK(ra.frames == rb.frames);
        std::vector<uint8_t> da, db;
        read_wav(p1, &da);
        read_wav(p2, &db);
        CHECK(!da.empty());
        CHECK(da == db);                                                  // identical input => identical bytes
        session_destroy(a);
        session_destroy(b);
    }

    // --- 3. `bars` derives the length from the transport tempo (120 bpm, 4/4 => 1 bar = 2 s). ---
    {
        Session* s = make_tone_session(reg, sr);
        vivid::BounceRequest req; req.path = p1; req.bars = 1.0;   // no explicit seconds
        vivid::BounceResult res; std::string err;
        CHECK(vivid::bounce_session_to_wav(s, tr, req, res, &err));
        CHECK_NEAR(static_cast<double>(res.frames), 2.0 * sr, sr);        // ~2 s (± a block)
        session_destroy(s);
    }

    // --- 4. Path safety + argument validation: bad requests are rejected, no file written. ---
    {
        Session* s = make_tone_session(reg, sr);
        vivid::BounceResult r; std::string e;
        vivid::BounceRequest rel;  rel.path  = "relative.wav";       rel.seconds  = 1.0;
        vivid::BounceRequest ext;  ext.path  = (tmp / "x.mp3").string();  ext.seconds = 1.0;
        vivid::BounceRequest dots; dots.path = (tmp / ".." / "x.wav").string(); dots.seconds = 1.0;
        vivid::BounceRequest nodur; nodur.path = p1;                 nodur.seconds = 0.0;   // and bars 0
        CHECK(!vivid::bounce_session_to_wav(s, tr, rel,   r, &e));        // not absolute
        CHECK(!vivid::bounce_session_to_wav(s, tr, ext,   r, &e));        // wrong extension
        CHECK(!vivid::bounce_session_to_wav(s, tr, dots,  r, &e));        // contains ".."
        CHECK(!vivid::bounce_session_to_wav(s, tr, nodur, r, &e));        // no duration
        CHECK(!vivid::bounce_session_to_wav(nullptr, tr, nodur, r, &e));  // no session
        session_destroy(s);
    }

    std::error_code ec;
    std::filesystem::remove(p1, ec);
    std::filesystem::remove(p2, ec);
    return vivid::test::summary("test_audio_bounce");
}
