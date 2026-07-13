#include "audio/audio_graph.h"

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
    std::vector<int> preds, succs;
    for (const AudioGraphEdge& e : edges_) {
        if (e.to_id == id)   preds.push_back(e.from_id);
        if (e.from_id == id) succs.push_back(e.to_id);
    }
    remove_node(id);   // drops the node + its incident edges
    for (int p : preds) for (int s : succs) connect(p, s);   // heal: preds -> succs (connect dedups)
}

bool AudioGraph::connect(int from_id, int to_id) {
    if (from_id == to_id || node_index(from_id) < 0 || node_index(to_id) < 0) return false;
    for (const AudioGraphEdge& e : edges_)
        if (e.from_id == from_id && e.to_id == to_id) return false;   // dup
    edges_.push_back({ from_id, to_id });
    return true;
}

void AudioGraph::disconnect(int from_id, int to_id) {
    for (size_t i = 0; i < edges_.size(); ++i)
        if (edges_[i].from_id == from_id && edges_[i].to_id == to_id) { edges_.erase(edges_.begin() + i); return; }
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
    for (const AudioGraphEdge& e : edges_)
        if (e.to_id == out && e.from_id != id) preds.push_back(e.from_id);
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
    std::vector<std::vector<int>> preds(N);
    for (const AudioGraphEdge& e : edges_) {
        const int fi = node_index(e.from_id), ti = node_index(e.to_id);
        if (fi < 0 || ti < 0) continue;
        indeg[ti]++;
        if (static_cast<int>(preds[ti].size()) < kMaxInputs) preds[ti].push_back(fi);
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

    out.steps.clear();
    out.steps.reserve(N);
    for (int idx : order) {
        CompiledStep s;
        s.process = nodes_[idx].process;
        s.ctx = nodes_[idx].ctx;
        s.n_in = static_cast<int>(preds[idx].size());
        for (int k = 0; k < s.n_in; ++k) s.in_buf[k] = preds[idx][k];   // predecessor out_buf == its index
        s.out_buf = idx;
        out.steps.push_back(s);
    }
    out.buf_count = N;
    out.output_buf = (output_id_ >= 0) ? node_index(output_id_) : -1;
    return true;
}

}  // namespace vivid::audio
