#include "audio/audio_graph.h"

#include <algorithm>
#include <cstring>

namespace vivid::audio {

// ---- CompiledAudioGraph::run — the audio-thread executor (RT-safe) ----------------------
void CompiledAudioGraph::run(float* pool, uint32_t frames, uint32_t stride) const {
    const int scratch = buf_count;   // shared input-sum scratch buffer
    for (const CompiledStep& s : steps) {
        float* outL = pool + static_cast<size_t>(s.out_buf) * 2 * stride;
        float* outR = outL + stride;
        if (s.n_in == 0) {
            // Source: no audio inputs. Its processor writes out; a null processor is silence.
            if (s.process) s.process(s.ctx, nullptr, nullptr, outL, outR, frames);
            else { std::memset(outL, 0, frames * sizeof(float)); std::memset(outR, 0, frames * sizeof(float)); }
            continue;
        }
        // Sum the inputs into the scratch buffer (predecessors already ran — topo order).
        float* inL = pool + static_cast<size_t>(scratch) * 2 * stride;
        float* inR = inL + stride;
        {
            const float* a0L = pool + static_cast<size_t>(s.in_buf[0]) * 2 * stride;
            const float* a0R = a0L + stride;
            std::memcpy(inL, a0L, frames * sizeof(float));
            std::memcpy(inR, a0R, frames * sizeof(float));
        }
        for (int k = 1; k < s.n_in; ++k) {
            const float* akL = pool + static_cast<size_t>(s.in_buf[k]) * 2 * stride;
            const float* akR = akL + stride;
            for (uint32_t i = 0; i < frames; ++i) { inL[i] += akL[i]; inR[i] += akR[i]; }
        }
        // Process (null = passthrough: out = summed in).
        if (s.process) s.process(s.ctx, inL, inR, outL, outR, frames);
        else { std::memcpy(outL, inL, frames * sizeof(float)); std::memcpy(outR, inR, frames * sizeof(float)); }
    }
}

// ---- AudioGraph — the editable model (UI thread) ----------------------------------------
int AudioGraph::node_index(int id) const {
    for (size_t i = 0; i < nodes_.size(); ++i) if (nodes_[i].id == id) return static_cast<int>(i);
    return -1;
}

void AudioGraph::set_node_pos(int id, float x, float y) {
    const int i = node_index(id);
    if (i < 0) return;
    nodes_[i].ui_x = x; nodes_[i].ui_y = y; nodes_[i].positioned = true;
}
bool AudioGraph::node_pos(int id, float& x, float& y) const {
    const int i = node_index(id);
    if (i < 0 || !nodes_[i].positioned) return false;
    x = nodes_[i].ui_x; y = nodes_[i].ui_y;
    return true;
}

void AudioGraph::clear_positions() {
    for (AudioGraphNode& n : nodes_) { n.positioned = false; n.ui_x = 0.f; n.ui_y = 0.f; }
}

void AudioGraph::pin_param(int id, int p) {
    const int i = node_index(id);
    if (i < 0 || p < 0) return;
    auto& v = nodes_[i].pinned_params;
    if (std::find(v.begin(), v.end(), p) == v.end()) v.push_back(p);   // idempotent, add order
}
void AudioGraph::unpin_param(int id, int p) {
    const int i = node_index(id);
    if (i < 0) return;
    auto& v = nodes_[i].pinned_params;
    v.erase(std::remove(v.begin(), v.end(), p), v.end());
}
bool AudioGraph::is_param_pinned(int id, int p) const {
    const int i = node_index(id);
    if (i < 0) return false;
    const auto& v = nodes_[i].pinned_params;
    return std::find(v.begin(), v.end(), p) != v.end();
}
const std::vector<int>* AudioGraph::node_pinned(int id) const {
    const int i = node_index(id);
    return i < 0 ? nullptr : &nodes_[i].pinned_params;
}

int AudioGraph::add_node(bool is_source, bool is_output, ProcessFn fn, void* ctx, std::string label) {
    AudioGraphNode n;
    n.id = next_id_++;
    n.is_source = is_source;
    n.is_output = is_output;
    n.process = fn;
    n.ctx = ctx;
    n.label = std::move(label);
    nodes_.push_back(std::move(n));
    if (is_output && output_id_ < 0) output_id_ = nodes_.back().id;
    return nodes_.back().id;
}

void AudioGraph::remove_node(int id) {
    for (size_t i = 0; i < edges_.size();) {
        if (edges_[i].from_id == id || edges_[i].to_id == id) edges_.erase(edges_.begin() + i);
        else ++i;
    }
    const int idx = node_index(id);
    if (idx >= 0) nodes_.erase(nodes_.begin() + idx);
    if (output_id_ == id) output_id_ = -1;
}

void AudioGraph::remove_node_bridged(int id) {
    // Heal each SIGNAL separately: an audio in/out pair bridges as audio, a note in/out pair as
    // notes. Bridging across kinds would silently turn a note wire into an audio wire (and vice
    // versa) — a plausible-looking graph that means something else entirely.
    std::vector<int> preds[2], succs[2];   // [0] = audio, [1] = note
    for (const AudioGraphEdge& e : edges_) {
        const int k = (e.kind == EdgeKind::Note) ? 1 : 0;
        if (e.to_id == id)   preds[k].push_back(e.from_id);
        if (e.from_id == id) succs[k].push_back(e.to_id);
    }
    remove_node(id);   // drops the node + its incident edges
    for (int k = 0; k < 2; ++k) {
        const EdgeKind kind = (k == 0) ? EdgeKind::Audio : EdgeKind::Note;
        for (int p : preds[k])
            for (int s : succs[k]) connect(p, s, kind);   // connect dedups
    }
}

bool AudioGraph::connect(int from_id, int to_id, EdgeKind kind) {
    if (from_id == to_id || node_index(from_id) < 0 || node_index(to_id) < 0) return false;
    for (const AudioGraphEdge& e : edges_)   // dup of the SAME kind (one pair may carry both signals)
        if (e.from_id == from_id && e.to_id == to_id && e.kind == kind) return false;
    edges_.push_back({ from_id, to_id, kind });
    return true;
}

void AudioGraph::disconnect(int from_id, int to_id, EdgeKind kind) {
    for (size_t i = 0; i < edges_.size(); ++i)
        if (edges_[i].from_id == from_id && edges_[i].to_id == to_id && edges_[i].kind == kind) {
            edges_.erase(edges_.begin() + i);
            return;
        }
}

void AudioGraph::set_note_ports(int id, bool note_in, bool note_out) {
    const int idx = node_index(id);
    if (idx < 0) return;
    nodes_[idx].note_in = note_in;
    nodes_[idx].note_out = note_out;
}

void AudioGraph::set_node_processor(int id, ProcessFn fn, void* ctx) {
    const int idx = node_index(id);
    if (idx >= 0) { nodes_[idx].process = fn; nodes_[idx].ctx = ctx; }
}

// An effect lands at the END of the signal path: everything that fed Output now feeds the new
// node instead, and the new node feeds Output. (A node added with no wiring is inaudible, which
// reads as "the app ignored me".)
void AudioGraph::splice_before_output(int id) {
    if (node_index(id) < 0) return;
    const int out = output_id_;
    if (out < 0 || out == id) return;
    std::vector<int> preds;
    for (const AudioGraphEdge& e : edges_)   // AUDIO predecessors only: a note edge into Output is
        if (e.kind == EdgeKind::Audio && e.to_id == out && e.from_id != id) preds.push_back(e.from_id);
    for (int p : preds) { disconnect(p, out); connect(p, id); }
    connect(id, out);
}

// A source is a parallel head of the graph: it fans in to Output alongside any existing source
// (Output sums its inputs — that's the primitive key-splits and layers are built from).
int AudioGraph::fan_in_to_output(int id, int (*make_output)(void* user), void* user) {
    if (node_index(id) < 0) return -1;
    int out = output_id_;
    if (out < 0) {   // a bare graph: without a sink the source would be silent
        out = make_output ? make_output(user) : add_node(false, true, nullptr, nullptr, "out");
        if (out < 0) return -1;
        output_id_ = out;
    }
    connect(id, out);
    return out;
}

void AudioGraph::clear() { nodes_.clear(); edges_.clear(); output_id_ = -1; }   // keeps next_id_
void AudioGraph::reset() { nodes_.clear(); edges_.clear(); output_id_ = -1; next_id_ = 0; }

// Kahn's-algorithm topological sort; assigns each node its own output buffer (== node index).
// Returns false if a cycle is present (out untouched). Nodes unreachable from any edge still
// run (as isolated sources/sinks) — every node gets a step.
bool AudioGraph::compile(CompiledAudioGraph& out) const {
    const int N = static_cast<int>(nodes_.size());
    if (N == 0) { out.steps.clear(); out.buf_count = 0; out.output_buf = -1; return true; }

    std::vector<int> indeg(N, 0);
    // Predecessor buffers per node index (each predecessor's out_buf == its node index).
    std::vector<std::vector<int>> preds(N);        // AUDIO inputs (summed)
    std::vector<std::vector<int>> note_preds(N);   // NOTE inputs (merged) — ADR-0015
    for (const AudioGraphEdge& e : edges_) {
        const int fi = node_index(e.from_id), ti = node_index(e.to_id);
        if (fi < 0 || ti < 0) continue;
        // BOTH kinds constrain execution order: a note effect must run before the instrument it
        // feeds, exactly as an audio effect must run before its consumer. One DAG, one topo sort.
        indeg[ti]++;
        if (e.kind == EdgeKind::Note) {
            if (static_cast<int>(note_preds[ti].size()) < kMaxNoteInputs) note_preds[ti].push_back(fi);
        } else {
            if (static_cast<int>(preds[ti].size()) < kMaxInputs) preds[ti].push_back(fi);
        }
    }

    std::vector<int> order;
    order.reserve(N);
    std::vector<int> queue;
    queue.reserve(N);
    for (int i = 0; i < N; ++i) if (indeg[i] == 0) queue.push_back(i);
    // Deterministic order: process the queue as a stable FIFO.
    size_t head = 0;
    while (head < queue.size()) {
        const int u = queue[head++];
        order.push_back(u);
        for (const AudioGraphEdge& e : edges_) {
            if (node_index(e.from_id) != u) continue;
            const int v = node_index(e.to_id);
            if (v < 0) continue;
            if (--indeg[v] == 0) queue.push_back(v);
        }
    }
    if (static_cast<int>(order.size()) != N) return false;   // cycle — keep the caller's last good plan

    // Note buffers: one per note-EMITTING node, allocated only for nodes that actually emit. A
    // graph with no note edges gets none, so the note pool costs nothing until it is used.
    std::vector<int> note_buf(N, -1);
    int nnb = 0;
    for (int i = 0; i < N; ++i) if (nodes_[i].note_out) note_buf[i] = nnb++;

    out.steps.clear();
    out.steps.reserve(N);
    for (int idx : order) {
        CompiledStep s;
        s.process = nodes_[idx].process;
        s.ctx = nodes_[idx].ctx;
        s.n_in = static_cast<int>(preds[idx].size());
        for (int k = 0; k < s.n_in; ++k) s.in_buf[k] = preds[idx][k];   // predecessor out_buf == its index
        s.out_buf = idx;
        s.note_out_buf = note_buf[idx];
        s.n_note_in = 0;
        for (int p : note_preds[idx])                  // only a note-emitting predecessor has a buffer
            if (note_buf[p] >= 0 && s.n_note_in < kMaxNoteInputs) s.note_in_buf[s.n_note_in++] = note_buf[p];
        out.steps.push_back(s);
    }
    out.buf_count = N;
    out.note_buf_count = nnb;
    out.output_buf = (output_id_ >= 0) ? node_index(output_id_) : -1;
    return true;
}

}  // namespace vivid::audio
