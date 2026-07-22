// End-to-end smoke test for AUDIO operator packages: compile the example-audio package
// (an audio_effect + an instrument + a generator, all authored with VIVID_REGISTER, gpu-free),
// load the dylibs, and prove they actually RUN through the native audio runtime — not just load.
// This exercises the loaded-dylib adapter's audio forwarding + the descriptor capability flags (a
// loaded audio op must report has_process_audio and be instantiable via audio_op_create), and the
// v14 audio-role path: a loaded generator declares VIVID_AUDIO_ROLE_GENERATOR via vivid_audio_role,
// so it classifies as a scene generator instead of an instrument.
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

    // 1. Manifest: three operators with kinds that imply no wgpu link.
    PackageManifest m = parse_package_manifest(PKG_AUDIO_DIR);
    CHECK(m.ok);
    CHECK(m.operators.size() == 3);
    for (const auto& op : m.operators) CHECK(op.gpu == false);   // audio_effect/instrument/generator => no wgpu

    // 2. Install (compile both operators to a temp managed dir).
    const std::string out = (fs::temp_directory_path() / "vivid_audio_pkg_out").string();
    fs::remove_all(out);
    setenv("VIVID_OPERATORS_DIR", out.c_str(), 1);
    PackageInstallResult r = install_package(PKG_AUDIO_DIR);
    CHECK(r.ok);
    CHECK(r.compiles.size() == 3);
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
    CHECK(reg.has("PulseGen"));

    // 4. Capability flags come from the dylib (not the adapter's C++ inheritance): all three are
    //    audio operators, none is gpu/frame.
    for (const char* nm : { "Drive", "SineSynth", "PulseGen" }) {
        const VividOperatorDescriptor* d = reg.descriptor_for(nm);
        CHECK(d != nullptr);
        CHECK(d->has_process_audio == 1);
        CHECK(d->has_process_gpu == 0);
        CHECK(d->has_process_frame == 0);
    }

    // 4b. v14: PulseGen's declared VIVID_AUDIO_ROLE_GENERATOR travels through the vivid_audio_role
    //     export -> LoadedOperator::declared_audio_role -> the host descriptor, so it classifies as
    //     a scene GENERATOR (not an instrument), while the effect/instrument stay DEFAULT.
    CHECK(reg.descriptor_for("PulseGen")->audio_role == VIVID_AUDIO_ROLE_GENERATOR);
    CHECK(reg.descriptor_for("Drive")->audio_role == VIVID_AUDIO_ROLE_DEFAULT);
    CHECK(reg.descriptor_for("SineSynth")->audio_role == VIVID_AUDIO_ROLE_DEFAULT);
    CHECK(audio_op_is_gen_op(reg, "PulseGen"));
    CHECK(!audio_op_is_gen_op(reg, "SineSynth"));

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

    // 7. The generator RUNS: PulseGen is a silent source that emits notes from the transport. At
    //    beat 0 with every=1 it fires a pulse — the loaded dylib writes a note-on to note_out.
    {
        AudioOp* gen = audio_op_create(reg, "PulseGen");
        CHECK(gen != nullptr);
        CHECK(audio_op_is_source(gen));
        std::vector<float> L(N, 0.f), R(N, 0.f);
        std::vector<session::NoteEvent> nout(16);
        uint32_t nout_n = 0;
        audio_op_process(gen, L.data(), R.data(), N, sr, 120.f, 4, 0.0,
                         nullptr, 0, nout.data(), static_cast<uint32_t>(nout.size()), &nout_n);
        CHECK(nout_n > 0);     // a pulse fired
        CHECK(nout[0].on);     // note-on at the step boundary
        audio_op_destroy(gen);
    }

    fs::remove_all(out);
    return vivid::test::summary("test_package_audio");
}
