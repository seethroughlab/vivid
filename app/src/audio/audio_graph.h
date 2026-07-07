#pragma once
// The per-track audio signal graph (ADR-0012). A track's audio devices form a
// directed acyclic graph — sources (instruments) and effects wired by edges, terminating
// in one Output node — rather than a fixed linear chain. Multiple edges into a node imply
// a stereo SUM of its inputs (the single primitive from which parallel chains, racks, and
// sends fall out).
//
// This header is the pure topology + execution core: decoupled from VST3 / native AudioOp
// (the host binds a processor callback per node). It is header-free of any RT-unsafe deps
// so it is fully unit-testable. Threading contract (mirrors the rest of the engine, see
// docs/thread-safety.md):
//   - AudioGraph (the editable model) is mutated on the UI thread only.
//   - compile() builds an immutable CompiledAudioGraph (topo order + buffer assignments) on
//     the UI thread. Cycles are rejected (the caller keeps the last good plan).
//   - CompiledAudioGraph::run() executes on the audio thread over a caller-preallocated
//     buffer pool — NO allocation, NO locking. The host swaps a freshly compiled plan in via
//     the usual generation-counter + try_lock edit-mirror.
#include <cstdint>
#include <string>
#include <vector>

namespace vivid::audio {

// A node's processor. Reads `inL`/`inR` (the summed inputs, planar stereo; nullptr for a
// source with no audio inputs) and writes `outL`/`outR` (`frames` samples). Called on the
// audio thread — must itself be RT-safe. nullptr = passthrough (out = summed in; silence if
// no input) — used for the Output node and unbound nodes.
using ProcessFn = void (*)(void* ctx, const float* inL, const float* inR,
                           float* outL, float* outR, uint32_t frames);

constexpr int kMaxInputs = 16;   // fan-in cap per node (excess inputs are dropped + logged by the host)

// A node in the editable graph. `id` is stable (survives reorders/removals); the host binds
// `process`/`ctx` to the underlying VST3 handle or native AudioOp.
struct AudioGraphNode {
    int         id = -1;
    bool        is_source = false;   // no audio inputs expected (instrument/generator)
    bool        is_output = false;   // the track sink (exactly one per graph)
    ProcessFn   process = nullptr;
    void*       ctx = nullptr;
    std::string label;               // for debugging / UI
};

struct AudioGraphEdge { int from_id = -1; int to_id = -1; };

// One executable step (built by compile(), read-only on the audio thread). Fixed-size input
// list so run() touches no heap.
struct CompiledStep {
    ProcessFn process = nullptr;
    void*     ctx = nullptr;
    int       in_buf[kMaxInputs];
    int       n_in = 0;
    int       out_buf = -1;
};

// The immutable RT plan. `steps` are in topological order; each node owns one output buffer
// (buf_count buffers), plus one shared input-scratch buffer at index buf_count. The pool the
// host passes to run() must hold (buf_count + 1) * 2 * stride floats.
struct CompiledAudioGraph {
    std::vector<CompiledStep> steps;      // topo order; only READ on the audio thread
    int output_buf = -1;                  // buffer holding the track's final L/R (-1 => silence)
    int buf_count = 0;                     // node output buffers; scratch buffer = buf_count

    // Buffers needed = (buf_count + 1). Each buffer is 2*stride floats (planar L then R).
    int pool_buffers() const { return buf_count + 1; }

    // Execute over `pool` (planar stereo per buffer: buffer b's L at pool + b*2*stride, R at
    // +stride). Writes the track output into pool[output_buf]. RT-safe: no alloc, no lock.
    void run(float* pool, uint32_t frames, uint32_t stride) const;
};

// The editable per-track graph (UI thread). Source of truth for topology; the host attaches
// processors to nodes before compiling.
class AudioGraph {
public:
    // --- topology edits (UI thread) ---
    int  add_node(bool is_source, bool is_output, ProcessFn fn, void* ctx, std::string label = "");
    void remove_node(int id);                    // also drops incident edges
    // Remove a node but first "heal" the graph: reconnect each of its predecessors to each of its
    // successors, so signal keeps flowing when a middle node is deleted (delete-and-bridge).
    void remove_node_bridged(int id);
    bool connect(int from_id, int to_id);        // returns false if it would create a self-loop / dup
    void disconnect(int from_id, int to_id);
    void set_node_processor(int id, ProcessFn fn, void* ctx);   // rebind after a device swap
    // Drop all nodes/edges but KEEP the id counter, so a rebuilt/edited graph never recycles a
    // stale id (edges saved against an old node can't silently re-bind to a new one). Use this
    // whenever the graph is the authoritative source of topology (free rewiring, persistence).
    void clear();
    // Full reset INCLUDING the id counter → deterministic 0-based ids on the next build. Use only
    // when the whole graph is regenerated from scratch each time (the derived linear-chain path) or
    // when loading a session (ids come from the saved file, so the counter is re-seeded past them).
    void reset();

    // --- queries ---
    const std::vector<AudioGraphNode>& nodes() const { return nodes_; }
    const std::vector<AudioGraphEdge>& edges() const { return edges_; }
    int  output_id() const { return output_id_; }
    void set_output_id(int id) { output_id_ = id; }
    int  node_index(int id) const;               // -1 if absent

    // Compile into an immutable plan. Returns false on a cycle (out is left unchanged so the
    // caller keeps its last good plan). buf_count == node count; each node's out_buf == its
    // index in nodes_. Steps are emitted in topological order.
    bool compile(CompiledAudioGraph& out) const;

private:
    std::vector<AudioGraphNode> nodes_;
    std::vector<AudioGraphEdge> edges_;
    int output_id_ = -1;
    int next_id_ = 0;
};

}  // namespace vivid::audio
