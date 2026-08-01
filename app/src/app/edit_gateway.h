#pragma once

#include <chrono>
#include <string>
#include <nlohmann/json.hpp>

#include "app/undo_manager.h"

// ADR-0017 — the edit gateway (command sink). ONE object that every document edit (UI and MCP)
// routes through, so undo capture, grouping, and (later) the dirty-flag/autosave of ADR-0018 all
// live in one place. Vivid's analogue of vivid-classic's RuntimeCommandSink.
//
// It does NOT perform edits — callers still mutate the model as they do today, then call note_edit()
// (or bracket a gesture with begin_group/end_group). The gateway's job is to snapshot the canonical
// document projection at the right granularity and to restore it (with a smart audio tier) on undo.
namespace vivid {

struct App;

class EditGateway {
public:
    explicit EditGateway(App& app) : app_(app) {}

    // Seed the history with the current document as the baseline (entry 0). Call on every document
    // load / new session and at startup. Clears any prior history (undo dies with the document).
    void reset_baseline();

    // Like reset_baseline(), but for the post-open settle: re-seeds the baseline to the current
    // document only while it is still changing frame-to-frame (async plugin rebind after an open),
    // and returns true once it has stabilized. The frame loop calls this each frame after a
    // load/new until it returns true, so undo can't restore a half-built session (Phase-2 F1).
    bool reseed_baseline_if_settling();

    // The sink hook: record that a logical edit just happened. `label` describes it ("Delete Node");
    // `coalesce_key` (e.g. "param:<node>/<name>") merges a rapid run of the SAME key into one entry
    // (the MCP-automation fallback) — empty means a structural edit that never coalesces. Inside an
    // open group the snapshot is deferred to end_group(); a structural edit (empty key) mid-group
    // force-closes the group as its own boundary.
    void note_edit(const std::string& label, const std::string& coalesce_key = {});

    // Bracket a gesture or bulk action into ONE undo entry. begin/end may nest; the snapshot is taken
    // once when the outermost group ends, and only if some note_edit dirtied it.
    void begin_group(const std::string& label);
    void end_group();
    // Reconcile a leaked group (a gesture whose end_group was lost, e.g. a release delivered to
    // another window): commit it if dirty and reset depth to 0. Called on the next press and by the
    // frame watchdog when the mouse is up, so a lost release can't permanently wedge undo.
    void close_open_group() { if (group_depth_ > 0) force_close_group(); }
    bool group_open() const { return group_depth_ > 0; }

    // Take the deferred snapshot for any edit noted this frame. Call at the END of the tick (after
    // draw, so draw-time settling like node auto-positioning is included), before end_frame_audit().
    void commit_frame();

    bool undo();
    bool redo();
    bool can_undo() const { return undo_.can_undo(); }
    bool can_redo() const { return undo_.can_redo(); }
    const std::string& undo_label() const { return undo_.undo_label(); }
    const std::string& redo_label() const { return undo_.redo_label(); }

    // Called at the end of each frame tick. Under VIVID_UNDO_AUDIT it asserts the document did not
    // change unless a gateway edit fired this frame (or a group is open) — turning any un-routed
    // mutation site into a failing assertion instead of a silent wrong-undo. A no-op otherwise, but
    // it always resets the per-frame edit flag.
    void end_frame_audit();

    // A monotonically bumped counter so the frame loop can cheaply detect "history changed" and
    // refresh the Edit-menu labels without polling strings every frame.
    unsigned revision() const { return revision_; }

    // ADR-0018: the app-level document-dirty flag (there was none before — only editor-local bits).
    // True once any real edit lands (a snapshot pushed, or an undo/redo restore); cleared on save and
    // on load/new. Drives the macOS edited-dot, the save-confirm on New/Open/Quit, and autosave.
    bool dirty() const { return dirty_; }
    void mark_saved() { dirty_ = false; }
    // ADR-0018: flag the document as having unsaved changes without pushing an undo entry — used after
    // a launch-time autosave recovery (the recovered doc IS the baseline, but differs from disk).
    void mark_dirty() { dirty_ = true; ++revision_; }

private:
    nlohmann::json canonical_projection_now() const;   // session_to_json(0-dims) -> canonical
    void seed_baseline(const nlohmann::json& proj);    // clear history + install proj as entry 0
    void force_close_group();
    void push_snapshot(const nlohmann::json& proj, bool replace_top, const std::string& label);
    void restore(const nlohmann::json& target);        // apply a snapshot with the smart audio tier

    App&        app_;
    UndoManager undo_;
    nlohmann::json cached_;          // last canonical projection (for the audit + baseline)
    bool        edited_this_frame_ = false;
    bool        dirty_ = false;          // ADR-0018: unsaved document changes since load/new/save
    unsigned    revision_ = 0;

    // deferred capture (snapshot taken at commit_frame(), post-settle)
    bool        pending_ = false;
    std::string pending_label_;
    std::string pending_key_;

    // grouping
    int         group_depth_ = 0;
    bool        group_dirty_ = false;
    std::string group_label_;

    // coalescing fallback (rapid same-key edits, chiefly MCP set_param)
    std::string last_key_;
    std::chrono::steady_clock::time_point last_time_{};
};

}  // namespace vivid
