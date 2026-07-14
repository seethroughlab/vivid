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
constexpr int kMaxNoteInputs = 8;   // note fan-in cap per node (note inputs MERGE, as audio sums)

// ADR-0015: an edge carries one of TWO signals. Audio edges sum (the primitive ADR-0012 chose);
// Note edges merge. A graph with no Note edges behaves exactly as it did before notes existed —
// that equivalence is the migration gate.
enum class EdgeKind : uint8_t { Audio = 0, Note = 1 };

// A node in the editable graph. `id` is stable (survives reorders/removals); the host binds
// `process`/`ctx` to the underlying VST3 handle or native AudioOp.
struct AudioGraphNode {
    int         id = -1;
    bool        is_source = false;   // no audio inputs expected (instrument/generator)
    bool        is_output = false;   // the track sink (exactly one per graph)
    // ADR-0015: what this node does with NOTES. An instrument takes them; a MidiIn node emits them;
    // a note effect (arpeggiator, chord, transpose) does both. Notes are a signal in the graph, not
    // an invisible per-track broadcast, so the node must say which ports it has.
    bool        note_in  = false;
    bool        note_out = false;
    ProcessFn   process = nullptr;
    void*       ctx = nullptr;
    std::string label;               // for debugging / UI
    // UI-thread only (never read by the audio thread): the node's editor position. `positioned`
    // is false until the user drags it / a session restores it — until then the editor auto-lays out.
    float       ui_x = 0.f, ui_y = 0.f;
    bool        positioned = false;
};

struct AudioGraphEdge {
    int      from_id = -1;
    int      to_id   = -1;
    EdgeKind kind    = EdgeKind::Audio;   // absent in old projects => Audio (unchanged behavior)
};

// One executable step (built by compile(), read-only on the audio thread). Fixed-size input lists
// so run() touches no heap.
struct CompiledStep {
    ProcessFn process = nullptr;
    void*     ctx = nullptr;
    int       in_buf[kMaxInputs];
    int       n_in = 0;
    int       out_buf = -1;
    // ADR-0015: the note buffers this step reads (merged) and the one it writes. -1 = none, which
    // is every node in a graph with no note edges — i.e. the whole engine as it stands today.
    int       note_in_buf[kMaxNoteInputs];
    int       n_note_in = 0;
    int       note_out_buf = -1;
};

// The immutable RT plan. `steps` are in topological order; each node owns one output buffer
// (buf_count buffers), plus one shared input-scratch buffer at index buf_count. The pool the
// host passes to run() must hold (buf_count + 1) * 2 * stride floats.
struct CompiledAudioGraph {
    std::vector<CompiledStep> steps;      // topo order; only READ on the audio thread
    int output_buf = -1;                  // buffer holding the track's final L/R (-1 => silence)
    int buf_count = 0;                     // node output buffers; scratch buffer = buf_count
    // ADR-0015: how many NOTE buffers the host must preallocate (one per note-emitting node).
    // 0 for every graph that has no note edges — the note pool costs nothing until it's used.
    int note_buf_count = 0;

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
    // ADR-0015: declare a node's NOTE ports (an instrument takes notes; a MidiIn node emits them;
    // a note effect does both). Default: neither, which is every node in the graph as it stands.
    void set_note_ports(int id, bool note_in, bool note_out);
    void remove_node(int id);                    // also drops incident edges
    // Remove a node but first "heal" the graph: reconnect each of its predecessors to each of its
    // successors, so signal keeps flowing when a middle node is deleted (delete-and-bridge).
    void remove_node_bridged(int id);
    // Connect from -> to. `kind` picks the signal (audio sums at the destination; notes merge).
    // Returns false on a self-loop or a duplicate edge OF THAT KIND (a node may feed another both
    // audio and notes — a note effect that also passes audio, say — so the pair is not unique).
    bool connect(int from_id, int to_id, EdgeKind kind = EdgeKind::Audio);
    void disconnect(int from_id, int to_id, EdgeKind kind = EdgeKind::Audio);
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
    // Editor node position (UI thread; persisted). set marks the node positioned; get returns
    // false when the node is absent or has never been placed (→ the editor auto-lays it out).
    void set_node_pos(int id, float x, float y);
    bool node_pos(int id, float& x, float& y) const;

    // Compile into an immutable plan. Returns false on a cycle (out is left unchanged so the
    // caller keeps its last good plan). buf_count == node count; each node's out_buf == its
    // index in nodes_. Steps are emitted in topological order.
    bool compile(CompiledAudioGraph& out) const;

    // --- the two ways a newly added node gets wired (shared by every add path: the native ops,
    // the plugin nodes, the chooser, a browser drop) ---
    //
    // An EFFECT is spliced in just before Output: every P->Output becomes P->new->Output, so it
    // lands at the end of the signal path and is immediately audible.
    void splice_before_output(int id);
    // A SOURCE fans in to Output, in parallel with any existing source (two sources with disjoint
    // key ranges = a key-split). Materializes the Output node if the graph is bare, since a source
    // with nowhere to go is silent. Returns the Output node's id (it may have just been created),
    // or -1 if `id` is not a real node. `make_output` supplies the Output node when one is needed
    // (the host must keep its own per-node bind array in step) — pass nullptr to let the graph
    // create a bare one.
    int fan_in_to_output(int id, int (*make_output)(void* user), void* user = nullptr);

private:
    std::vector<AudioGraphNode> nodes_;
    std::vector<AudioGraphEdge> edges_;
    int output_id_ = -1;
    int next_id_ = 0;
};

}  // namespace vivid::audio
