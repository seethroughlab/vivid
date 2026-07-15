#include "app/edit_gateway.h"

#include "app/app.h"
#include "ui/node_graph.h"
#include "persist.h"
#include "persist_undo.h"

#include <cassert>
#include <cstdio>

namespace vivid {

using json = nlohmann::json;

nlohmann::json EditGateway::canonical_projection_now() const {
    if (!app_.session || !app_.graph) return json::object();
    // Dummy window dims: the projection strips the whole "window" block, so they never matter.
    return canonical_document_projection(session_to_json(app_.session, *app_.graph, 0, 0, 0.f, 0.f));
}

void EditGateway::reset_baseline() {
    cached_ = canonical_projection_now();
    undo_.clear();
    undo_.push(cached_.dump(), false, {});   // entry 0 — the pre-edit baseline
    edited_this_frame_ = true;                // this frame's document change is intentional
    last_key_.clear();
    group_depth_ = 0; group_dirty_ = false; group_label_.clear();
    ++revision_;
}

void EditGateway::push_snapshot(const json& proj, bool replace_top, const std::string& label) {
    undo_.push(proj.dump(), replace_top, label);
    cached_ = proj;
    ++revision_;
}

void EditGateway::note_edit(const std::string& label, const std::string& coalesce_key) {
    edited_this_frame_ = true;

    if (group_depth_ > 0) {
        if (coalesce_key.empty()) {
            // A structural edit reached us mid-group: close the group as its own clean boundary
            // (its post-state already includes this edit, since capture is post-state).
            force_close_group();
        } else {
            // A fine-grained edit inside an open group: fold in; the snapshot is taken at end_group.
            group_dirty_ = true;
            if (group_label_.empty()) group_label_ = label;
        }
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    bool replace_top = false;
    if (!coalesce_key.empty() && coalesce_key == last_key_) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time_).count();
        replace_top = (ms <= 300);
    }
    push_snapshot(canonical_projection_now(), replace_top, label);
    if (coalesce_key.empty()) { last_key_.clear(); }
    else { last_key_ = coalesce_key; last_time_ = now; }
}

void EditGateway::begin_group(const std::string& label) {
    if (group_depth_ == 0) {
        last_key_.clear();          // a fresh group must not coalesce with the preceding edit
        group_label_ = label;
        group_dirty_ = false;
    }
    ++group_depth_;
}

void EditGateway::end_group() {
    if (group_depth_ == 0) return;   // unbalanced end — ignore
    if (--group_depth_ == 0 && group_dirty_) {
        last_key_.clear();
        push_snapshot(canonical_projection_now(), false, group_label_);
    }
    if (group_depth_ == 0) { group_label_.clear(); group_dirty_ = false; }
}

void EditGateway::force_close_group() {
    group_depth_ = 0;
    if (group_dirty_) {
        last_key_.clear();
        push_snapshot(canonical_projection_now(), false, group_label_);
    }
    group_label_.clear();
    group_dirty_ = false;
}

bool EditGateway::undo() {
    std::string dump;
    if (!undo_.undo(dump)) return false;
    json target = json::parse(dump, nullptr, /*allow_exceptions=*/false);
    if (target.is_discarded()) return false;
    restore(target);
    return true;
}

bool EditGateway::redo() {
    std::string dump;
    if (!undo_.redo(dump)) return false;
    json target = json::parse(dump, nullptr, false);
    if (target.is_discarded()) return false;
    restore(target);
    return true;
}

void EditGateway::restore(const nlohmann::json& target) {
    if (!app_.session || !app_.graph) return;
    // Skip the (plugin-instantiating) audio path when the tracks block is unchanged; otherwise Full.
    // (ParamsOnly is added in G3; until then a track-topology change pays the Full rebuild.)
    const json current = canonical_projection_now();
    const RestoreAudio tier = audio_block_equal(target, current) ? RestoreAudio::Skip : RestoreAudio::Full;
    int ww = 0, wh = 0; float sx = 0.f, dh = 0.f;
    session_from_json_scoped(target, app_.session, *app_.graph, ww, wh, sx, dh, tier);
    cached_ = target;
    edited_this_frame_ = true;
    ++revision_;
}

void EditGateway::end_frame_audit() {
#ifdef VIVID_UNDO_AUDIT
    if (group_depth_ == 0 && !edited_this_frame_ && app_.session && app_.graph) {
        const json now = canonical_projection_now();
        if (now != cached_) {
            // Report which top-level keys drifted so an un-routed edit site is diagnosable.
            std::string keys;
            for (auto it = now.begin(); it != now.end(); ++it)
                if (!cached_.contains(it.key()) || cached_[it.key()] != it.value()) { keys += it.key(); keys += ' '; }
            std::fprintf(stderr, "[vivid] UNDO AUDIT FAIL: document changed with no gateway edit this "
                                 "frame — a mutation site bypasses EditGateway::note_edit. keys: %s\n", keys.c_str());
            if (keys.find("graph") != std::string::npos && now.contains("graph") && cached_.contains("graph")) {
                for (auto it = now["graph"].begin(); it != now["graph"].end(); ++it)
                    if (!cached_["graph"].contains(it.key()) || cached_["graph"][it.key()] != it.value())
                        std::fprintf(stderr, "  graph.%s drift; now=%s\n", it.key().c_str(),
                                     it.value().dump().substr(0, 300).c_str());
            }
            if (!std::getenv("VIVID_UNDO_AUDIT_LOG"))
                assert(false && "un-routed document mutation (see VIVID_UNDO_AUDIT)");
            cached_ = now;   // log-only mode: adopt so each distinct drift reports once
        }
    }
#endif
    edited_this_frame_ = false;
}

}  // namespace vivid
