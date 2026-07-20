// ADR-0025 (vst3_host split, PR-D): the node-id-keyed PARAM API + curated-inspector metadata,
// extracted verbatim from vst3_host.cpp. These read/write a graph node's params (native op / VST3 /
// CLAP) and describe them for the UI (widget type, enum choices, display strings, pin state) — pure
// main-thread accessors, no audio-thread render and no graph-topology mutation. The public functions
// are the session C API (declared in vst3_host.h); the graph-node + param-base helpers are file-local
// and moved with them. Reaches a track via graph_track(), now shared through vst3_host_internal.h.
#include "audio/vst3_host_internal.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vivid::session {

// Node-id-keyed param access. The chain-index model (audio_op_at, -1/0+) can't address a node
// in a non-linear graph, so params are addressed by stable node id (from the introspection API).
// Works for both derived (linear) and authoritative graphs — agnodes holds each node's bound op.
static vivid::AudioOp* graph_node_op(Track* tr, int node_id) {   // caller holds tr->gmtx
    const int idx = tr->agraph.node_index(node_id);
    return (idx >= 0 && idx < static_cast<int>(tr->agnodes.size())) ? tr->agnodes[idx].op : nullptr;
}
// A VST3 graph node → its handle (whose `params`/`controller`/`param_q` back the dock param band,
// exactly as the old linear device view did). Null for native/sampler/output nodes.
static Vst3Handle* graph_node_handle(Track* tr, int node_id) {   // caller holds tr->gmtx
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return nullptr;
    const GNodeBind& nb = tr->agnodes[idx];
    return (nb.handle && (nb.kind == GNKind::Vst3Inst || nb.kind == GNKind::Vst3Fx)) ? nb.handle : nullptr;
}
// A CLAP graph node → its handle (params in PLAIN units; edits go via its SPSC param_q). Null otherwise.
static ClapHandle* graph_node_clap(Track* tr, int node_id) {   // caller holds tr->gmtx
    const int idx = tr->agraph.node_index(node_id);
    if (idx < 0 || idx >= static_cast<int>(tr->agnodes.size())) return nullptr;
    const GNodeBind& nb = tr->agnodes[idx];
    return (nb.clap && (nb.kind == GNKind::ClapInst || nb.kind == GNKind::ClapFx)) ? nb.clap : nullptr;
}
// The dock param band edits a graph node's params — native op params OR a VST3 node's exposed
// (automatable) params, in NORMALIZED 0..1 space like the old linear knob grid.
int session_audio_graph_node_param_count(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_count(op);
    if (Vst3Handle* h = graph_node_handle(tr, node_id)) return static_cast<int>(h->params.size());
    if (ClapHandle* c = graph_node_clap(tr, node_id)) return static_cast<int>(c->params.size());
    return 0;
}
const char* session_audio_graph_node_param_name(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_name(op, p);
    if (Vst3Handle* h = graph_node_handle(tr, node_id))
        return (p >= 0 && p < static_cast<int>(h->params.size())) ? h->params[p].name.c_str() : "";
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? c->params[p].name.c_str() : "";
    return "";
}
int session_audio_graph_node_param_hint(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_hint(op, p);
    return 0;   // VST3 params → DEFAULT (a knob)
}
// Base/min/max WITHOUT taking gmtx — for callers already holding it (the resolved accessor below).
// Base for a native op is its `pvals` (never written by the audio thread); for a plugin it is the
// plugin's own current value (there is no host-side base for plugins yet — ADR-0022, a P2 gap).
static float graph_param_base_nolock(Track* tr, int node_id, int p) {
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_get(op, p);
    if (Vst3Handle* h = graph_node_handle(tr, node_id))
        return (h->controller && p >= 0 && p < static_cast<int>(h->params.size()))
                   ? static_cast<float>(h->controller->getParamNormalized(h->params[p].id)) : 0.f;
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size()))
                   ? static_cast<float>(clap_param_value(c, c->params[p].id)) : 0.f;
    return 0.f;
}
static float graph_param_min_nolock(Track* tr, int node_id, int p) {
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_min(op, p);
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? static_cast<float>(c->params[p].min) : 0.f;
    return 0.f;   // VST3 params are normalized
}
static float graph_param_max_nolock(Track* tr, int node_id, int p) {
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) return vivid::audio_op_param_max(op, p);
    if (ClapHandle* c = graph_node_clap(tr, node_id))
        return (p >= 0 && p < static_cast<int>(c->params.size())) ? static_cast<float>(c->params[p].max) : 1.f;
    return 1.f;
}

float session_audio_graph_node_param_get(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return graph_param_base_nolock(tr, node_id, p);   // the BASE (ADR-0022): the user's value
}
float session_audio_graph_node_param_min(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return graph_param_min_nolock(tr, node_id, p);
}
float session_audio_graph_node_param_max(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 1.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return graph_param_max_nolock(tr, node_id, p);
}

// ADR-0022: the RESOLVED value — base + every control edge driving this param, right now. The UI
// draws the live dot with this; MCP reports it as "value". It runs the same control_resolve() the
// audio thread runs, reading each modulator's published output (ctl_pub), so the dot cannot drift
// from what you hear. For an unmodulated param it is exactly the base.
float session_audio_graph_node_param_resolved(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0.f;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    float v = graph_param_base_nolock(tr, node_id, p);
    const float lo = graph_param_min_nolock(tr, node_id, p);
    const float hi = graph_param_max_nolock(tr, node_id, p);
    for (const vivid::audio::AudioGraphEdge& e : tr->agraph.edges()) {
        if (e.kind != vivid::audio::EdgeKind::Control || e.to_id != node_id || e.dest_param != p) continue;
        const int mi = tr->agraph.node_index(e.from_id);   // modulator node index == ctl_pub index
        if (mi < 0 || mi >= kGraphMaxNodes) continue;
        const float src = tr->ctl_pub[mi].load(std::memory_order_relaxed);
        v = vivid::audio::control_resolve(v, src, e.shape, lo, hi);   // stacks, like the audio thread
    }
    return v;
}
// 1 if any control edge drives this param (the UI's "modulated" affordance — the teal ring).
int session_audio_graph_node_param_wired(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    for (const vivid::audio::AudioGraphEdge& e : tr->agraph.edges())
        if (e.kind == vivid::audio::EdgeKind::Control && e.to_id == node_id && e.dest_param == p) return 1;
    return 0;
}
void session_audio_graph_node_param_set(Session* s, int t, int node_id, int p, float v) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (vivid::AudioOp* op = graph_node_op(tr, node_id)) { vivid::audio_op_param_set(op, p, v); return; }
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && p >= 0 && p < static_cast<int>(h->params.size())) {
        const ParamID id = h->params[p].id;
        v = v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
        h->param_q.push(id, v);                                            // → audio thread (RT-safe SPSC)
        if (h->controller) h->controller->setParamNormalized(id, v);       // → plugin GUI reflection
        return;
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && p >= 0 && p < static_cast<int>(c->params.size())) {
        double val = std::clamp(static_cast<double>(v), c->params[p].min, c->params[p].max);
        c->param_q.push(c->params[p].id, val);   // plain value → audio thread emits a CLAP param event
    }
}
// --- Richer param metadata (widget-by-type + curated inspector). VST3 + CLAP. ---
// A param's discreteness drives the widget: 0 discrete steps = continuous (→ slider/knob), a
// 2-state switch (→ bool/toggle), or a small named list (→ enum/dropdown). Enum labels + the human
// display value come from the plugin's own formatter (VST3 getParamStringByValue / CLAP value_to_text).
// VST3 encodes discreteness as step_count; CLAP as the IS_STEPPED flag over a plain [min,max] range.
static constexpr int kMaxEnumChoices = 24;   // beyond this a discrete param is a slider, not a giant dropdown

int session_audio_graph_node_param_type(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0 /*VIVID_PARAM_FLOAT*/;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && p >= 0 && p < static_cast<int>(h->params.size())) {
        const int sc = h->params[p].step_count;
        if (sc == 1) return 2 /*VIVID_PARAM_BOOL*/;
        if (sc  > 1 && sc + 1 <= kMaxEnumChoices) return 1 /*VIVID_PARAM_INT (enum)*/;
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && p >= 0 && p < static_cast<int>(c->params.size())) {
        if (c->params[p].flags & CLAP_PARAM_IS_STEPPED) {
            const int span = static_cast<int>(std::lround(c->params[p].max - c->params[p].min));   // discrete values = span+1
            if (span == 1) return 2 /*VIVID_PARAM_BOOL*/;
            if (span > 1 && span + 1 <= kMaxEnumChoices) return 1 /*VIVID_PARAM_INT (enum)*/;
        }
    }
    return 0 /*VIVID_PARAM_FLOAT*/;   // native ops / continuous / too-many-steps
}
int session_audio_graph_node_param_choice_count(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && p >= 0 && p < static_cast<int>(h->params.size())) {
        const int sc = h->params[p].step_count;
        return (sc > 1 && sc + 1 <= kMaxEnumChoices) ? sc + 1 : 0;   // a discrete list of sc+1 named values
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && p >= 0 && p < static_cast<int>(c->params.size())) {
        if (c->params[p].flags & CLAP_PARAM_IS_STEPPED) {
            const int span = static_cast<int>(std::lround(c->params[p].max - c->params[p].min));
            if (span > 1 && span + 1 <= kMaxEnumChoices) return span + 1;
        }
    }
    return 0;
}
const char* session_audio_graph_node_param_choice_label(Session* s, int t, int node_id, int p, int choice) {
    static thread_local std::string buf; buf.clear();
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && h->controller && p >= 0 && p < static_cast<int>(h->params.size())) {
        const int sc = h->params[p].step_count;
        if (sc > 1 && choice >= 0 && choice <= sc) {
            String128 str{};
            const double norm = static_cast<double>(choice) / static_cast<double>(sc);   // VST3: normalized position of the choice
            if (h->controller->getParamStringByValue(h->params[p].id, norm, str) == kResultOk)
                buf = vst3_tchar_to_utf8(str);
        }
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && c->ext_params && c->ext_params->value_to_text
        && p >= 0 && p < static_cast<int>(c->params.size())) {
        const auto& pe = c->params[p];
        const double val = pe.min + static_cast<double>(choice);   // CLAP: the choice's plain value
        if (choice >= 0 && val <= pe.max) {
            char tmp[128];
            if (c->ext_params->value_to_text(c->plugin, pe.id, val, tmp, sizeof tmp)) buf = tmp;
        }
    }
    return buf.c_str();
}
const char* session_audio_graph_node_param_display(Session* s, int t, int node_id, int p) {
    static thread_local std::string buf; buf.clear();
    Track* tr = graph_track(s, t); if (!tr) return "";
    std::lock_guard<std::mutex> lk(tr->gmtx);
    if (Vst3Handle* h = graph_node_handle(tr, node_id); h && h->controller && p >= 0 && p < static_cast<int>(h->params.size())) {
        const ParamID id = h->params[p].id;
        String128 str{};
        if (h->controller->getParamStringByValue(id, h->controller->getParamNormalized(id), str) == kResultOk) {
            buf = vst3_tchar_to_utf8(str);
            if (!h->params[p].units.empty()) { buf += ' '; buf += h->params[p].units; }
        }
    }
    if (ClapHandle* c = graph_node_clap(tr, node_id); c && c->ext_params && c->ext_params->value_to_text
        && p >= 0 && p < static_cast<int>(c->params.size())) {
        char tmp[128];
        if (c->ext_params->value_to_text(c->plugin, c->params[p].id, clap_param_value(c, c->params[p].id), tmp, sizeof tmp))
            buf = tmp;
    }
    return buf.c_str();   // "" for native ops → caller keeps its numeric fallback
}
// Is this graph node a plugin (VST3 or CLAP)? Drives the curated inspector (vs the native knob strip).
int session_audio_graph_node_is_plugin(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return (graph_node_handle(tr, node_id) != nullptr || graph_node_clap(tr, node_id) != nullptr) ? 1 : 0;
}
// Curated inspector param set (pure curation). Stored on the AudioGraphNode (UI thread; persisted).
void session_audio_graph_node_param_pin(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.pin_param(node_id, p);
}
void session_audio_graph_node_param_unpin(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    tr->agraph.unpin_param(node_id, p);
}
int session_audio_graph_node_param_is_pinned(Session* s, int t, int node_id, int p) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    return tr->agraph.is_param_pinned(node_id, p) ? 1 : 0;
}
int session_audio_graph_node_param_pinned_count(Session* s, int t, int node_id) {
    Track* tr = graph_track(s, t); if (!tr) return 0;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const std::vector<int>* v = tr->agraph.node_pinned(node_id);
    return v ? static_cast<int>(v->size()) : 0;
}
int session_audio_graph_node_param_pinned_at(Session* s, int t, int node_id, int i) {
    Track* tr = graph_track(s, t); if (!tr) return -1;
    std::lock_guard<std::mutex> lk(tr->gmtx);
    const std::vector<int>* v = tr->agraph.node_pinned(node_id);
    return (v && i >= 0 && i < static_cast<int>(v->size())) ? (*v)[i] : -1;
}

}  // namespace vivid::session
