// test_dialog_manager_lockfile.cpp — Phase 6b modal state tests.
//
// Covers the open/close flow of DialogManager::LockfileFindingsState. The
// drawing path is only exercised in a live GLFW context; these tests focus
// on the state transitions that determine when the modal renders, how its
// findings copy behaves, and how close() resets.
#include "ui/dialogs/dialog_manager.h"
#include "ui/ui_command_sink.h"
#include "runtime/packages/project_lockfile.h"

#include <cstdio>
#include <string>

#include "test_helpers.h"

using namespace vivid::ui;

namespace {

// Minimal stub implementing UICommandSink's pure virtuals. DialogManager
// doesn't call any of these in the lockfile-findings path; it's only a ctor
// requirement.
struct StubSink : UICommandSink {
    void set_param(const std::string&, const std::string&, float) override {}
    void add_node(const std::string&, const std::string&) override {}
    void remove_node(const std::string&) override {}
    void connect(const std::string&, const std::string&) override {}
    void disconnect(const std::string&, const std::string&) override {}
    void set_connection_remap(const std::string&, const std::string&,
                              float, float, float, float, bool, uint8_t) override {}
    void set_node_layout(const std::string&, float, float) override {}
    void set_resolution(const std::string&, uint32_t, uint32_t) override {}
    void add_midi_mapping(const std::string&, const std::string&,
                          int, int, float, float) override {}
    void remove_midi_mapping(const std::string&, const std::string&) override {}
    void update_midi_mapping(const std::string&, const std::string&,
                             float, float) override {}
    void set_string_param(const std::string&, const std::string&,
                          const std::string&) override {}
};

vivid::LockfileStatus make_critical_status() {
    vivid::LockfileStatus s;
    s.overall = vivid::LockfileOverall::Mismatch;
    s.findings.push_back({"missing_package", vivid::LockfileSeverity::Critical,
                          "pkg-a", "not installed", "install pkg-a@1.0.0"});
    s.findings.push_back({"incompatible_update", vivid::LockfileSeverity::Critical,
                          "pkg-b", "major version bump", "reinstall pkg-b@2.0.0"});
    s.findings.push_back({"graph_content_drift", vivid::LockfileSeverity::Info,
                          "graph", "hash changed", ""});
    return s;
}

void test_default_state_is_closed() {
    StubSink sink;
    DialogManager dm(sink);
    check(!dm.lockfile_findings_open(),
          "default: lockfile_findings_open() is false");
    check(dm.lockfile_findings.status.findings.empty(),
          "default: internal findings empty");
    check(dm.lockfile_findings.scroll_y == 0.0f,
          "default: scroll_y is zero");
}

void test_open_flips_state() {
    StubSink sink;
    DialogManager dm(sink);
    dm.open_lockfile_findings(make_critical_status());
    check(dm.lockfile_findings_open(),
          "open: lockfile_findings_open() is true after open_lockfile_findings()");
    check(dm.lockfile_findings.status.findings.size() == 3,
          "open: all three findings stored");
    check(dm.lockfile_findings.status.overall == vivid::LockfileOverall::Mismatch,
          "open: overall copied");
    check(dm.lockfile_findings.scroll_y == 0.0f,
          "open: scroll_y reset to 0");
}

void test_status_is_copied_not_referenced() {
    StubSink sink;
    DialogManager dm(sink);
    auto status = make_critical_status();
    dm.open_lockfile_findings(status);

    // Mutate the source; the modal must not observe the change.
    status.findings.clear();
    status.overall = vivid::LockfileOverall::Match;

    check(dm.lockfile_findings.status.findings.size() == 3,
          "copy semantics: modal retains original findings after source mutation");
    check(dm.lockfile_findings.status.overall == vivid::LockfileOverall::Mismatch,
          "copy semantics: modal retains original overall");
}

void test_close_resets_state() {
    StubSink sink;
    DialogManager dm(sink);
    dm.open_lockfile_findings(make_critical_status());
    dm.lockfile_findings.scroll_y = 123.0f;  // simulate scroll

    dm.close_lockfile_findings();
    check(!dm.lockfile_findings_open(),
          "close: lockfile_findings_open() is false");
    check(dm.lockfile_findings.scroll_y == 0.0f,
          "close: scroll_y reset to 0");
}

void test_reopen_replaces_findings() {
    StubSink sink;
    DialogManager dm(sink);
    dm.open_lockfile_findings(make_critical_status());
    check(dm.lockfile_findings.status.findings.size() == 3, "first open: 3 findings");

    vivid::LockfileStatus smaller;
    smaller.overall = vivid::LockfileOverall::CompatibleDrift;
    smaller.findings.push_back({"compatible_update", vivid::LockfileSeverity::Info,
                                 "pkg-c", "minor bump", ""});
    dm.open_lockfile_findings(smaller);
    check(dm.lockfile_findings.status.findings.size() == 1,
          "reopen: findings replaced with new status");
    check(dm.lockfile_findings.status.overall == vivid::LockfileOverall::CompatibleDrift,
          "reopen: overall updated");
}

void test_any_open_includes_lockfile_findings() {
    StubSink sink;
    DialogManager dm(sink);
    check(!dm.any_open(), "any_open: default false");
    dm.open_lockfile_findings(make_critical_status());
    check(dm.any_open(),
          "any_open: includes lockfile_findings after open");
    dm.close_lockfile_findings();
    check(!dm.any_open(), "any_open: false after close");
}

void test_wants_keyboard_does_not_include_lockfile_findings() {
    // The modal is read-only: it should NOT block keyboard input.
    StubSink sink;
    DialogManager dm(sink);
    dm.open_lockfile_findings(make_critical_status());
    check(!dm.wants_keyboard(),
          "wants_keyboard: false even when lockfile findings modal is open");
}

}  // namespace

int main() {
    test_default_state_is_closed();
    test_open_flips_state();
    test_status_is_copied_not_referenced();
    test_close_resets_state();
    test_reopen_replaces_findings();
    test_any_open_includes_lockfile_findings();
    test_wants_keyboard_does_not_include_lockfile_findings();

    if (failures == 0) {
        std::fprintf(stderr, "All dialog_manager_lockfile tests passed.\n");
        return 0;
    }
    std::fprintf(stderr, "%d dialog_manager_lockfile failure(s).\n", failures);
    return 1;
}
