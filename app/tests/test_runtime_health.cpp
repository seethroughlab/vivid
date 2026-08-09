// Headless test for the runtime-health rollup + serialization (P4.3). severity() and
// to_json() are App-free, so this exercises the pass/warn/error policy + the JSON shape
// on synthetic snapshots (the live collect_health needs App/GPU and is smoke-tested).
#include "app/runtime_health.h"
#include "test_helpers.h"

using vivid::HealthSnapshot;
using vivid::Severity;
using vivid::severity;
using vivid::to_json;

int main() {
    // A clean, healthy snapshot -> Ok.
    HealthSnapshot ok;
    ok.gpu_ok = true; ok.control_running = true;
    ok.op_nodes = 3; ok.op_types = 8; ok.missing_ops = 0;
    CHECK(severity(ok) == Severity::Ok);

    // Uncaptured GPU errors -> Warning (survivable).
    HealthSnapshot warn = ok;
    warn.gpu_errors = 2;
    CHECK(severity(warn) == Severity::Warning);

    // Control server down -> Warning.
    HealthSnapshot warn2 = ok;
    warn2.control_running = false;
    CHECK(severity(warn2) == Severity::Warning);

    // Device lost -> Error (hard breakage).
    HealthSnapshot err1 = ok;
    err1.gpu_ok = false;
    CHECK(severity(err1) == Severity::Error);

    // A graph referencing a vanished operator -> Error.
    HealthSnapshot err2 = ok;
    err2.missing_ops = 1;
    CHECK(severity(err2) == Severity::Error);

    // Error dominates a co-occurring warning.
    HealthSnapshot both = ok;
    both.gpu_errors = 5; both.missing_ops = 2;
    CHECK(severity(both) == Severity::Error);

    // An unfed Output is empty-by-design (benign): it must NOT change severity (P2-03).
    HealthSnapshot unfed = ok;
    unfed.output_fed = false;
    CHECK(severity(unfed) == Severity::Ok);
    // ...and it doesn't rescue a real fault either.
    HealthSnapshot unfed_broken = err2;   // missing_ops == 1
    unfed_broken.output_fed = false;
    CHECK(severity(unfed_broken) == Severity::Error);

    // JSON shape: severity string + the dimension objects.
    ok.app_version = "9.9.9"; ok.gpu_last_error = "";
    auto j = to_json(ok);
    CHECK(j["severity"] == "ok");
    CHECK(j["app_version"] == "9.9.9");
    CHECK(j["graph"]["op_nodes"] == 3);
    CHECK(j["graph"]["missing_ops"] == 0);
    CHECK(j["graph"]["output_fed"] == true);   // default (fed)
    CHECK(to_json(unfed)["graph"]["output_fed"] == false);
    CHECK(j["gpu"]["ok"] == true);
    CHECK(j["control"]["running"] == true);
    CHECK(!j["gpu"].contains("last_error"));   // omitted when empty

    auto je = to_json(err1);
    CHECK(je["severity"] == "error");

    // --- ADR-0031 audio realtime health rollup ----------------------------------------------------
    // Over-budget callbacks / handoff skips are recoverable pressure -> passive Warning.
    HealthSnapshot ab = ok;
    ab.audio_over_budget = 1;
    CHECK(severity(ab) == Severity::Warning);
    HealthSnapshot sk = ok;
    sk.audio_handoff_skips = 3;
    CHECK(severity(sk) == Severity::Warning);

    // Sustained render bail-to-silence at/over the threshold -> Error (audible dropout).
    HealthSnapshot bail = ok;
    bail.audio_bailout_error_threshold = 4;
    bail.audio_render_bailouts = 4;
    CHECK(severity(bail) == Severity::Error);
    // ...but below the threshold is not yet an Error (still Ok if nothing else is wrong).
    HealthSnapshot bail_lo = ok;
    bail_lo.audio_bailout_error_threshold = 4;
    bail_lo.audio_render_bailouts = 2;
    CHECK(severity(bail_lo) == Severity::Ok);
    // A zero threshold means "no audio data" — bailouts must never raise severity then.
    HealthSnapshot no_thresh = ok;
    no_thresh.audio_bailout_error_threshold = 0;
    no_thresh.audio_render_bailouts = 999;
    CHECK(severity(no_thresh) == Severity::Ok);
    // Bailout Error dominates a co-occurring over-budget warning.
    HealthSnapshot bail_ob = bail;
    bail_ob.audio_over_budget = 7;
    CHECK(severity(bail_ob) == Severity::Error);
    // All-zero audio fields leave the pre-audio rollup unchanged.
    CHECK(severity(ok) == Severity::Ok);

    // JSON carries the audio counters under the existing "audio" object (backward-compatible shape).
    HealthSnapshot aj = ok;
    aj.audio_over_budget = 2; aj.audio_handoff_skips = 5; aj.audio_max_callback_us = 4200;
    auto ja = to_json(aj);
    CHECK(ja["audio"]["over_budget"] == 2);
    CHECK(ja["audio"]["handoff_skips"] == 5);
    CHECK(ja["audio"]["max_callback_us"] == 4200);
    CHECK(ja["severity"] == "warning");

    return vivid::test::summary("test_runtime_health");
}
