// End-to-end smoke test for AUDIO operator packages: compile the example-audio package
// (an audio_effect + an instrument authored with VIVID_REGISTER, gpu-free), load both dylibs,
// and prove they actually RUN through the native audio runtime — not just load. This exercises
// the loaded-dylib adapter's audio forwarding + the descriptor capability flags (a loaded audio
// op must report has_process_audio and be instantiable via audio_op_create).
#include "packages/package_manifest.h"
#include "packages/package_manager.h"
#include "gpu/operator_loader.h"
#include "gpu/operator_scan.h"
#include "gpu/op_runtime.h"
#include "audio/audio_op_runtime.h"
#include "midi/midi_clip.h"          // session::NoteEvent
#include "test_helpers.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

int main() {
    using namespace vivid;
    namespace fs = std::filesystem;

    // 1. Manifest: two operators with kinds that imply no wgpu link.
    PackageManifest m = parse_package_manifest(PKG_AUDIO_DIR);
    CHECK(m.ok);
    CHECK(m.operators.size() == 2);
    for (const auto& op : m.operators) CHECK(op.gpu == false);   // audio_effect/instrument => no wgpu

    // 2. Install (compile both operators to a temp managed dir).
    const std::string out = (fs::temp_directory_path() / "vivid_audio_pkg_out").string();
    fs::remove_all(out);
    setenv("VIVID_OPERATORS_DIR", out.c_str(), 1);
    PackageInstallResult r = install_package(PKG_AUDIO_DIR);
    CHECK(r.ok);
    CHECK(r.compiles.size() == 2);
    for (const auto& c : r.compiles) {
        if (!c.success) std::fprintf(stderr, "  compile error:\n%s\n", c.error_output.c_str());
        CHECK(c.success);
        CHECK(fs::exists(c.dylib_path));
    }

    // 3. Load + register both dylibs.
    OpRegistry reg;
    std::vector<std::unique_ptr<OperatorLoader>> loaders;
    for (const auto& c : r.compiles) {
        const std::string name = load_and_register_operator(c.dylib_path, reg, loaders);
        CHECK(!name.empty());
    }
    CHECK(reg.has("Drive"));
    CHECK(reg.has("SineSynth"));

    // 4. Capability flags come from the dylib (not the adapter's C++ inheritance): both are
    //    audio operators, neither is gpu/frame.
    for (const char* nm : { "Drive", "SineSynth" }) {
        const VividOperatorDescriptor* d = reg.descriptor_for(nm);
        CHECK(d != nullptr);
        CHECK(d->has_process_audio == 1);
        CHECK(d->has_process_gpu == 0);
        CHECK(d->has_process_frame == 0);
    }

    const uint32_t N = 64, sr = 48000;

    // 5. The effect RUNS: Drive is not a source; with defaults (drive=2, mix=1) it writes
    //    tanh(in*2). Feed 0.5 -> tanh(1.0) ~= 0.7616 (proves the adapter forwarded process_audio).
    {
        AudioOp* drive = audio_op_create(reg, "Drive");
        CHECK(drive != nullptr);
        CHECK(!audio_op_is_source(drive));
        std::vector<float> L(N, 0.5f), R(N, 0.5f);
        audio_op_process(drive, L.data(), R.data(), N, sr, 120.f, 4, 0.0, nullptr, 0);
        CHECK(std::fabs(L[0] - std::tanh(1.0f)) < 1e-3f);
        CHECK(std::fabs(R[0] - std::tanh(1.0f)) < 1e-3f);
        audio_op_destroy(drive);
    }

    // 6. The instrument RUNS: SineSynth is a source; a note-on makes a voice sound (non-zero out).
    {
        AudioOp* synth = audio_op_create(reg, "SineSynth");
        CHECK(synth != nullptr);
        CHECK(audio_op_is_source(synth));
        session::NoteEvent on{ 0u, true, static_cast<int16_t>(69), 1.0f, 1, 0.f };
        std::vector<float> L(N, 0.f), R(N, 0.f);
        audio_op_process(synth, L.data(), R.data(), N, sr, 120.f, 4, 0.0, &on, 1);
        float peak = 0.f; for (float x : L) peak = std::max(peak, std::fabs(x));
        CHECK(peak > 0.f);   // a sine voice is audible
        audio_op_destroy(synth);
    }

    fs::remove_all(out);
    return vivid::test::summary("test_package_audio");
}
