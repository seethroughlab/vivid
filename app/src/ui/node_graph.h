#pragma once
#include "ui/renderer_2d.h"
#include "gpu/shader_uniforms.h"
#include "gpu/visual_graph.h"
#include "mapping.h"
#include "ui/param_widget.h"   // NodeWidget + node_widget_kind (dock draw / input agree)
#include "ui/node_canvas.h"    // NodeView — the shared pan/zoom transform (ADR-0023)
#include "ui/graph_selection.h" // GraphSelection — shared multi-select set + marquee (ADR-0033 P1)
#include "ui/graph_canvas.h"   // GraphCanvas — the shared graph-area draw skeleton (ADR-0023 Layer 2)
#include "ui/graph_adapter.h"  // GraphModelAdapter — the shared node-enumeration contract (ADR-0023 Layer 1)
#include "ui/chooser.h"        // the shared Tab palette (also used by the audio graph)
#include "gpu/shader_library.h"  // ADR-0016: badge a shader row SHADER, not OP
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <set>
#include <utility>
#include <array>

namespace vivid { class EditGateway; }

namespace vivid::ui {

// ADR-0033 P2: a portable snapshot of a visual op node's full authored state — everything a clone
// must carry (op_type + position + base params + FILE/TEXT params + curated pins + asset + its
// incoming audio→param mappings). Deliberately id-free: a clone/paste mints a FRESH stable id.
struct NodeCapture {
    std::string op_type;
    float x = 0.f, y = 0.f;
    std::vector<float> base;               // base param values (index order; same op_type ⇒ same layout)
    std::vector<std::string> file_params;  // FILE/TEXT param strings (parallel to base; empty slots ok)
    std::vector<int> pinned;               // curated body-param indices
    std::string asset;                     // CustomShader .glsl (project-relative), "" if none
    std::vector<vivid::Mapping> maps;      // incoming audio→param mappings (full shaping); dest is the
                                           // SOURCE node's "node:<old_id>.<param>" — rewritten on spawn.
};
// A copied sub-selection: the captured nodes + the edges BETWEEN them (indices into `nodes`). Edges to
// nodes outside the selection are deliberately omitted (ADR-0033). edge = {from_idx, to_idx, dst_port, src_port}.
struct GraphClip {
    std::vector<NodeCapture> nodes;
    std::vector<std::array<int, 4>> edges;
};


// A minimal node editor on Renderer2D. Left: audio data-source nodes (each a live
// characteristic). Right: the rewireable visuals chain — op-nodes (Plasma/Video/
// Feedback/Blur) with texture input (left) and output (right) ports that you wire
// output->input, terminating in an Output node that drives the viewer. Data nodes
// wire into the ops' parameter ports (the audio->visual bridge). The chain itself
// lives in VisualGraph; this class owns the layout + interaction.
class NodeGraph : public GraphModelAdapter, public CardDelegate {
public:
    NodeGraph();
    ~NodeGraph();   // releases cached chooser-preview textures

    // ADR-0023 Layer 1: the shared node-enumeration contract (op nodes only; the bridge data-nodes
    // are a visuals-domain overlay). draw() drives the shared canvas card loop (GraphCanvas::draw_cards)
    // over it; before_card draws the active-output ring UNDER the card, after_card draws the visuals
    // overlay OVER it (label, texture-input ports, output port, ×, live thumbnail).
    void collect_nodes(std::vector<AdapterNode>& out) const override;
    int  selected_node_id() const override;
    void before_card(Renderer2D& r, const AdapterNode& n, int idx) const override;
    void after_card(Renderer2D& r, const AdapterNode& n, int idx) const override;

    void set_source_by_id(const std::string& source, float v);   // one-shot publish by string id (cold path)
    // ADR-0028: intern a source id to a stable HANDLE, then publish by handle each frame — no per-frame
    // string build (caller keys the handle by a cheap integer) and no string hash (the registry cell +
    // the matching data-node are resolved once). `source_handle` builds the string once; `publish` is hot.
    int  source_handle(const std::string& source);
    void publish(int handle, float v);
    bool consumed(int handle) const;   // source_consumed(id) for an interned handle — no per-frame string
    // True if any source id starting with `prefix` is CONSUMED — wired to a param (a mapping) or shown
    // as a spawned data node. Lets the engine gate expensive per-node analysis (FFT) to what's on screen.
    bool source_consumed(const std::string& prefix) const;
    void apply_params();   // resolve each node's params from the registry; publish viz.* sources
    void add_data_node(const std::string& title, const std::string& source);
    void add_data_node(const std::string& title, int char_id);   // legacy (packed master/track char_id)

    void set_bounds(float x0, float y0, float x1, float y1);
    void set_frame(float x0, float y0, float x1, float y1);   // full visuals-column rect (grid + clip)
    void set_visual_graph(vivid::VisualGraph* vg);   // also seeds the default mapping

    // Persistence + inspection.
    int  node_count() const { return static_cast<int>(nodes_data_.size()); }
    void get_node(int i, float& x, float& y, std::string& source, std::string& title) const;
    // ADR-0053 A4: richer source-node introspection for get_session — the entity kind ("master"/"track"/
    // "other"), the bound stable track id (-1 unless track), and every named output (suffix + full source
    // id). Wired-ness is derived by the caller from the mappings list. Out-of-range = empty/0.
    void get_source_node_meta(int i, std::string& kind, int& track_id) const;
    int  source_node_output_count(int i) const;
    void get_source_node_output(int i, int o, std::string& suffix, std::string& source) const;
    void get_shader(float& x, float& y) const { x = sx_; y = sy_; }
    void reset_nodes();
    void add_node_raw(const std::string& title, const std::string& source, float x, float y);
    void add_node_raw(const std::string& title, int char_id, float x, float y);   // legacy load (decodes char_id)
    void set_shader(float x, float y) { sx_ = x; sy_ = y; }

    // ADR-0033 P5: per-node label — a user rename shown instead of the op_type. Empty = op_type.
    std::string op_name_at(int i) const;                 // "" if the node uses its op_type
    void        set_op_name_at(int i, const std::string& name);

    // ADR-0033 P5: sticky-note annotations — free-floating persisted text on the visual canvas,
    // addressable by MCP so intent lives in the session. Not graph nodes (no ports/wiring); a pure
    // explainability overlay. Positions are WORLD coords (drawn under the shared camera transform).
    int  add_annotation(float x, float y);               // new note at (x,y), returns its stable id
    void add_annotation_raw(int id, const std::string& text, float x, float y, float w, float h);  // load
    bool remove_annotation(int id);
    bool set_annotation_text(int id, const std::string& text);
    bool move_annotation(int id, float x, float y);
    int  annotation_count() const { return static_cast<int>(annos_.size()); }
    bool get_annotation(int i, int& id, std::string& text, float& x, float& y, float& w, float& h) const;
    std::string annotation_text_of(int id) const;        // "" if no such id
    int  annotation_at_world(double wx, double wy) const; // note id under a world point, -1 if none
    int  annotation_at_screen(double sx, double sy) const;// note id under a SCREEN point, -1 if none
    int  add_note_centered();                             // create a note at the viewport centre, notes undo; returns id

    // ADR-0033 P5: the shell primes the active in-canvas text edit each frame BEFORE draw, so the
    // note/label being typed shows the live buffer + a caret. anno_id / node_idx = the edit target
    // (-1 = none); `buf` points at the shell's edit buffer (Window::text_edit_buf). Not owned.
    void set_text_edit(int anno_id, int node_idx, const std::string* buf) {
        edit_anno_ = anno_id; edit_node_ = node_idx; edit_buf_ = buf;
    }
    const std::vector<vivid::Mapping>& mappings() const { return reg_.mappings(); }
    void add_mapping(const std::string& src, const std::string& dst, float amt,
                     float curve = 0.f, bool invert = false, float lo = 0.f, float hi = 1.f,
                     float attack = 0.f, float release = 0.f) {
        // ADR-0053 Phase B4 CUTOVER: an audio→visual mapping is now a typed control EDGE from a Reactive
        // SOURCE op, not a hidden registry wire (+ Phase-A teal card). This one chokepoint converts every
        // caller alike — connect_mapping, map_audio_to_visual_param, the source-card drag, AND legacy-file
        // load (transparent migration). Fall through to the registry only for the reverse path
        // (viz.*→param:) and non-audio node:* mappings (viz.*→node param), which keep the Phase-A card.
        if (dst.rfind("node:", 0) == 0) {
            if (add_audio_control_edge(src, dst, amt, curve, invert, lo, hi, attack, release)) return;
            ensure_source_node(src);   // non-audio node:* dest keeps its visible source card + registry wire
        }
        reg_.connect(src, dst, amt);
        if (auto* m = reg_.find(dst)) {
            m->curve = curve; m->invert = invert; m->out_lo = lo; m->out_hi = hi;
            m->attack = attack; m->release = release; m->primed = false;
        }
    }
    // ADR-0053 Phase B4: dedup/create the Reactive SOURCE VisualGraph node for a bridge audio source, and
    // convert a legacy (src,dst) audio→visual pair into that node + a control edge (false when src is not a
    // migratable audio source or the dst param/node is gone — the caller then keeps the registry wire).
    int  ensure_reactive_source_node(const std::string& op_type, int track_id);
    bool add_audio_control_edge(const std::string& src, const std::string& dst, float amount, float curve,
                                bool invert, float lo, float hi, float attack, float release);
    // ADR-0053 Phase B4: drop Phase-A AUDIO (master/track) source cards no registry mapping references —
    // their wire migrated to a control edge. Called after load-migration; keeps cards still backing a
    // registry mapping and all non-audio ("Other"/viz.*) cards.
    void prune_orphan_audio_source_nodes();
    // Advance mapping smoothing one frame (dt seconds). Call before apply_params().
    void advance_mappings(float dt) { reg_.advance(dt); }
    // Connect a bridge DATA node's source to op node `op_idx`'s param `local` (the same wire the drop path
    // makes). Records an undo note. Used by the param-reveal menu (Gesture B). False on invalid indices.
    bool connect_data_to_param(int data_idx, int op_idx, int local, int out_idx = 0);
    // A track (stable id) was deleted: drop the registry mappings sourced from it, its Phase-A Track source
    // card, AND (ADR-0053 B4/B5) its ReactiveTrack VisualGraph node — whose removal cascades to every
    // control edge that read its value lanes, so no edge dangles at a dead track. Returns # registry
    // mappings dropped.
    int drop_track_sources(int id);
    // Mapping shaping edits (from the M overview).
    void set_mapping_amount(const std::string& dst, float a) { if (auto* m = reg_.find(dst)) m->amount = a; }
    void set_mapping_curve(const std::string& dst, float c)  { if (auto* m = reg_.find(dst)) m->curve = c; }
    void toggle_mapping_invert(const std::string& dst)       { if (auto* m = reg_.find(dst)) m->invert = !m->invert; }
    void set_mapping_lo(const std::string& dst, float v)     { if (auto* m = reg_.find(dst)) m->out_lo = v; }
    void set_mapping_hi(const std::string& dst, float v)     { if (auto* m = reg_.find(dst)) m->out_hi = v; }
    // Return path: registry can drive any dest; main feeds extra sources
    // (the visuals' uniform values) and applies audio-param dests each frame.
    float dest_value(const std::string& dest) const { return reg_.dest_value(dest); }
    const std::string* source_of(const std::string& dest) const { return reg_.source_of(dest); }
    void  set_named_source(const std::string& id, float v) { reg_.set_source(id, v); }
    void  disconnect_dest(const std::string& dest) { reg_.disconnect(dest); }
    // Chain (op type + input edge + id + position + base params) persistence.
    int  op_count() const;
    void get_op(int i, int& input, int& id, float& x, float& y) const;   // op TYPE: op_kind_name(i)
    std::string op_type_at(int i) const;   // the node's operator name (persist key)
    void get_op_base(int i, float out[4]) const;
    void chain_load_begin();
    void chain_load_add(const std::string& op_type, int id, float x, float y);
    void chain_load_set_input(int i, int input);
    void chain_load_set_input_b(int i, int input);   // second input (2-in ops)
    int  op_input_b_at(int i) const;                 // -1 if none
    std::vector<int> op_inputs_at(int i) const;      // all texture input edges (trailing -1 trimmed) — persist
    std::vector<int> op_in_src_ports_at(int i) const; // parallel source-output-port array (trailing 0 trimmed) — persist
    void set_op_input_at(int i, int port, int src, int src_port = 0);  // wire src's out `src_port` -> node i input `port`
    std::string op_asset_at(int i) const;                       // node's data asset (CustomShader .glsl), "" if none
    void        set_op_asset_at(int i, const std::string& asset);
    bool        op_missing_at(int i) const;                     // node's op type never resolved (Ph4 P1-02)
    std::string op_orphan(int i) const;                         // preserved params JSON for a missing op, "" if none
    void        set_op_orphan(int i, const std::string& json);
    int         op_at(double sx, double sy) const;              // op node under a screen cursor, -1 if none
    int         op_at_world(double wx, double wy) const;        // op node under a WORLD point, -1 if none
    std::string op_source_path(int i) const;                    // absolute editable source (CustomShader .glsl), "" if none
    bool        swap_op_type(int i, const std::string& type);   // re-instantiate node i as `type` (id/input/pos kept)

    // Visual-node selection + inspector: the bottom dock edits the selected node's
    // base param values (the resolved value = clamp(base + live modulation)).
    int  selected_op() const { return sel_op_; }
    void select_op(int i);   // single-select node index i (replaces the multi-selection); -1 clears
    // ADR-0033 P1: the multi-selection (set of stable op-node ids). sel_op_ tracks its primary as an
    // index (the inspector target). Read-only to callers; mutated by the gesture FSM + keyboard.
    const GraphSelection& selection() const { return sel_; }
    // Keyboard multi-delete: remove the op with stable id `id` (Output is never removable). Structural
    // only — does NOT touch the selection or note undo; the caller re-syncs the set and notes one edit.
    bool delete_op_by_id(int id);

    // ADR-0033 P2: copy / paste / duplicate on the multi-selection. capture_ids snapshots the given
    // stable ids (Output skipped) + their internal edges + audio→param mappings into a GraphClip.
    // spawn_clip clones a clip at world offset (dx,dy): each node gets a FRESH id, internal edges are
    // remapped to the copies, external edges dropped, mappings replicated onto the new ids; returns the
    // new ids and notes one undo entry (`label`). duplicate_selection / paste_clipboard drive the set:
    // both re-select the copies so they're ready to group-drag (Phase 1). copy_selection fills the
    // in-session clipboard. All no-ops on an empty selection/clipboard.
    GraphClip capture_ids(const std::set<int>& ids) const;
    std::vector<int> spawn_clip(const GraphClip& clip, float dx, float dy, const char* label);
    int  duplicate_selection(float dx, float dy);         // clone sel_ in place; returns count
    void copy_selection();                                // capture sel_ into the clipboard
    std::vector<int> paste_clipboard(float dx, float dy); // spawn the clipboard; returns new ids
    bool clipboard_empty() const { return clipboard_.nodes.empty(); }
    // Keyboard editing (UX Ph4 F3): guarded delete of op `i` (Output is never removable) + how many
    // input ports op `i` accepts (for keyboard wiring's target-port cycling). delete_op folds the edit
    // into undo and moves the selection to a neighbour (staying in the visual graph). Returns false if
    // `i` is out of range or is the Output sink.
    bool delete_op(int i);
    int  op_input_port_count(int i) const;
    const char* op_kind_name(int i) const;               // "Plasma" / "Feedback" / ...
    int  op_param_count_at(int i) const;
    const char* op_param_label_at(int i, int local) const;
    float op_param_base_at(int i, int local) const;
    void  set_op_param_base_at(int i, int local, float v);
    float op_param_value_at(int i, int local) const;     // resolved (base + modulation)
    const char* op_file_param_at(int i, int local) const;         // FILE/TEXT param string ("" if none)
    void  set_op_file_param_at(int i, int local, const std::string& v);
    bool  op_param_wired_at(int i, int local) const;     // a data source OR a control edge drives it
    // ADR-0053 Phase B: control-edge persistence (op index i). Edges are saved with the target param by
    // NAME (resolved to an index on load, robust to param reorder) + the source's stable id + lane + shape.
    int   op_control_edge_count(int i) const;
    bool  get_op_control_edge(int i, int e, std::string& param, int& src_node, int& src_lane,
                              vivid::VisualControlShape& sh) const;
    void  load_op_control_edge(int i, const std::string& param, int src_node, int src_lane,
                               const vivid::VisualControlShape& sh);   // load path (no undo note)
    // Curated body params (pure UI curation): the ordered param indices SHOWN as rows on node `i`'s card.
    // = the node's pinned set UNION any wired param (a connection is always shown so a wire never dangles).
    // A fresh/uncurated node returns empty -> collapsed. Mirrors AudioNodeGraph::exposed_params.
    std::vector<int> exposed_params(int i) const;
    bool  is_param_pinned(int i, int local) const;
    void  pin_param(int i, int local);                   // idempotent; add order
    void  unpin_param(int i, int local);
    void  toggle_param_pin(int i, int local);
    // Gesture A: the node whose header curate-affordance (left chevron) is under (sx,sy), else -1.
    int   param_curate_hit(double sx, double sy) const;
    // Gesture B: a wire dropped on a node body (missing every visible port) parks a request here for the
    // app to open the reveal+connect menu. Returns true and clears when one is pending. node_idx = target
    // op node, src_data_node = the data node the wire started from, (sx,sy) = the drop screen position.
    bool  take_param_menu_request(int& node_idx, int& src_data_node, double& sx, double& sy);
    // Param metadata (from the operator descriptor) so the dock can pick a widget.
    int   op_param_type_at(int i, int local) const;         // VividParamType (FLOAT/INT/BOOL/...)
    int   op_param_hint_at(int i, int local) const;         // VividDisplayHint (DEFAULT/KNOB/COLOR/XY/...)
    float op_param_min_at(int i, int local) const;
    float op_param_max_at(int i, int local) const;
    int   op_param_choice_count_at(int i, int local) const; // >0 for enums
    const char* op_param_choice_label_at(int i, int local, int choice) const;

    // UI-4b: operator-exported custom editor. op_has_editor(i) is true when node i's op is a loaded
    // dylib that exports the editor ABI; op_draw_editor forwards the host-built context to it.
    bool op_has_editor(int i) const;
    VividEditorMetadata op_editor_metadata(int i) const;   // default/min size + title suffix
    void op_draw_editor(int i, VividEditorContext* ctx) const;

    void layout_nodes();                 // auto-arrange op nodes into a layered left->right layout
    void draw(Renderer2D& r);            // includes live node thumbnails (draw_texture)
    void draw_overlays(Renderer2D& r);   // chooser etc. — drawn after the node graph
    // ADR-0033 P1: shift/super carry the modifier state at press (kept GLFW-free — the shell computes
    // them). shift = additive/marquee, super(⌘) = toggle a card / additive marquee.
    bool on_down(double x, double y, bool shift, bool super);
    void on_move(double x, double y);
    void on_up(double x, double y);
    void zoom_at(double sx, double sy, float factor);   // scroll-wheel zoom around the cursor
    void get_view(float& ox, float& oy, float& scale) const { const NodeView& v = canvas_.view(); ox = v.ox; oy = v.oy; scale = v.scale; }
    void set_view(float ox, float oy, float scale) { canvas_.view() = { ox, oy, scale }; }

    // Operator chooser (Tab): a filtered palette that spawns a node at the cursor. The widget is
    // shared with the audio graph (ui/chooser.h); this class supplies the catalog + the spawn.
    bool chooser_open() const { return chooser_.open(); }
    // ADR-0016: so the chooser can badge a row SHADER (a file you can open and edit) rather
    // than OP (a compiled dylib). Optional — null just means every row reads as an op.
    void set_shader_library(const vivid::ShaderLibrary* lib) { shaders_ = lib; }
    // ADR-0018: op types quarantined this launch — shown greyed in the Tab chooser with a reason.
    void set_quarantined(std::set<std::string> q) { quarantined_ = std::move(q); }
    // ADR-0017: the undo command sink, so UI graph edits are captured (nullptr = no undo, e.g. tests).
    void set_edit_gateway(vivid::EditGateway* g) { edit_gateway_ = g; }
    void note_edit(const char* label, const char* key = "") { note_edit_(label, key); }   // for op-editor callbacks
    // The full audio→visual source catalog (label, canonical source id) the Tab chooser offers as
    // spawnable bridge nodes — built by the app from the live session (tracks × characteristics incl.
    // notes + fft, and per-node rms), since NodeGraph doesn't own the session. Set before chooser_show.
    void set_bridge_catalog(std::vector<std::pair<std::string, std::string>> cat) { bridge_catalog_ = std::move(cat); }
    void chooser_show(double sx, double sy);  // open at the cursor
    void chooser_hide() { chooser_.hide(); }
    void chooser_move(int dir) { chooser_.move(dir); }
    void chooser_backspace()   { chooser_.backspace(); }
    void chooser_char(unsigned int c) { chooser_.type(c); }
    void chooser_confirm();                   // spawn the selected entry
    // ADR-0033 P5: if the last chooser spawn created a sticky Note, returns its id (once, then clears)
    // so the caller can begin a text edit on it; -1 otherwise. The Window (which owns the text-edit
    // state) isn't reachable from here, so the input layer polls this after confirm/click.
    int  consume_pending_note_edit() { const int id = note_pending_edit_; note_pending_edit_ = -1; return id; }

    // ADR-0021/P3: create an op node at a screen position (as the chooser does) and, if given,
    // set the named FILE param to `file_value` (falls back to the node's first FILE param when
    // `file_param` is empty). Returns the new node index, or -1. Used by the file-drop handler.
    int drop_spawn(const std::string& op_type, double sx, double sy,
                   const std::string& file_param, const std::string& file_value);

private:
    static constexpr int kHistN = 64;   // data-node value history (rolling sparkline)
    // ADR-0053 Phase A2: audio sources are ENTITY nodes with multiple named value OUTPUTS. A "Master"
    // node exposes level/transient/low/mid/high + transport phases; a "Track <id>" node exposes its 8
    // characteristics. Each output row carries a right-edge port whose identity is a full bridge source
    // string ("master.low", "track_1.gate"); wiring an output->param is still just a MappingRegistry wire.
    enum class SourceKind { Master, Track, Other };
    struct SourceOutput { std::string suffix; std::string source; float value = 0.f;
                          float hist[kHistN] = {}; int hist_head = 0; };
    struct SourceNode { float x = 0, y = 0, w = 0, h = 0; SourceKind kind = SourceKind::Other;
                        int track_id = -1; std::string title; int flash = 0;
                        std::vector<SourceOutput> outs; };
    std::vector<SourceNode> nodes_data_;
    // ADR-0028 interning: one Pub per distinct published source. `cell` is a stable pointer into the
    // registry's value map (see MappingRegistry::intern_source); (node_idx,out_idx) caches the matching
    // source-node OUTPUT (for its live sparkline), re-resolved whenever `data_gen_` changes (the source
    // set grew/cleared — indices only ever append or clear, never shift, so they stay valid otherwise).
    struct Pub { float* cell; int node_idx; int out_idx; uint32_t data_gen; std::string id; };
    std::vector<Pub> pubs_;
    // ADR-0033 P5: sticky-note store (mirrors the DataNode pattern). id is stable within a session;
    // annos never shift index on remove would break nothing here since callers address by id.
    struct Annotation { int id; float x, y, w, h; std::string text; };
    std::vector<Annotation> annos_;
    int next_anno_id_ = 0;
    int anno_index_of_(int id) const;   // id -> index into annos_, -1 if none
    // ADR-0033 P5: the active in-canvas text edit, primed each frame by the shell (set_text_edit).
    int edit_anno_ = -1;                 // annotation id being typed into, or -1
    int edit_node_ = -1;                 // op node index being renamed, or -1
    const std::string* edit_buf_ = nullptr;   // the shell's live edit buffer (Window::text_edit_buf)
    std::unordered_map<std::string, int> handle_by_id_;   // dedup: source id -> pubs_ index (idempotent)
    uint32_t data_gen_ = 0;                                // bumped on any data_ add/clear
    std::vector<std::pair<std::string, std::string>> bridge_catalog_;   // (label, source id) — Tab chooser sources
    vivid::MappingRegistry reg_;
    float sx_ = 900.f, sy_ = 488.f;   // persisted shader-node position (get_shader/set_shader)
    float bx0_ = 520.f, by0_ = 448.f, bx1_ = 1272.f, by1_ = 792.f;  // node-layout / hit-test bounds (inset)
    float fx0_ = 512.f, fy0_ = 440.f, fx1_ = 1280.f, fy1_ = 800.f;  // full visuals-column rect (clip + grid)
    bool  bounds_init_ = false;

    // Op-node layout (parallel to vg_->nodes()); the chain lives in VisualGraph.
    std::vector<std::pair<float, float>> op_pos_;
    bool op_pos_init_ = false;

    // 0 none, 1 data-node drag, 2 op-node drag, 3 data->param wire, 4 op->op wire, 5 pan,
    // 6 marquee (ADR-0033 P1), 7 annotation drag (ADR-0033 P5)
    int    drag_mode_ = 0;
    int    drag_idx_ = -1;     // dragged node (data for mode 1, op for mode 2)
    int    anno_drag_ = -1;    // ADR-0033 P5: annotation id being dragged (mode 7), -1 = none
    int    wire_from_ = -1;    // data node (mode 3) or op node (mode 4) the wire starts at
    int    wire_from_out_ = -1;// ADR-0053 A2: which OUTPUT row of the source node the wire started at (mode 3)
    int    pmreq_node_ = -1;   // Gesture B: pending param-reveal-menu request (target op node index)
    int    pmreq_src_  = -1;   // Gesture B: the data node the dropped wire started from
    double pmreq_sx_ = 0, pmreq_sy_ = 0;   // Gesture B: the drop SCREEN position (where to open the menu)
    double dx_ = 0, dy_ = 0, cx_ = 0, cy_ = 0;
    int    sel_op_ = -1;     // selected visual node (inspector target = sel_'s primary), -1 = none
    GraphSelection sel_;     // ADR-0033 P1: the multi-selection (stable op-node ids); view-state, never persisted
    GraphClip clipboard_;    // ADR-0033 P2: the in-session copy buffer (⌘C fills it, ⌘V spawns it)
    // Marquee gesture (drag_mode_ 6): the rubber-band corners in WORLD coords + whether it's additive (⌘).
    double marq_x0_ = 0, marq_y0_ = 0, marq_x1_ = 0, marq_y1_ = 0;
    bool   marq_add_ = false;
    // Group-drag (drag_mode_ 2): each selected op's stable id + its op_pos_ at grab time, so every
    // selected node moves by the same world delta. Snapshotted on press, cleared on release.
    std::vector<std::pair<int, std::pair<float, float>>> grp_start_;
    GraphCanvas canvas_;   // ADR-0023 Layer 2: the shared draw skeleton AND the owner of the pan/zoom camera (#1)
    float  pan_last_x_ = 0.f, pan_last_y_ = 0.f;     // last cursor during a canvas pan
    void to_world(double sx, double sy, double& wx, double& wy) const { canvas_.view().to_world(sx, sy, wx, wy); }

    Chooser chooser_;                  // the shared Tab palette (ui/chooser.h)
    void chooser_spawn(const Chooser::Entry& e);   // create the node the chooser handed back
    int  note_pending_edit_ = -1;      // ADR-0033 P5: id of a just-spawned sticky Note awaiting text edit (-1 = none)

    // ADR-0050: bundled per-op preview thumbnails drawn in the add-node chooser. Lazily decoded +
    // uploaded on first draw, cached by slug (a null view = "no preview, draw the accent dot"), and
    // released in the destructor. Keyed by slugify(op type name).
    struct PreviewTex { WGPUTexture tex = nullptr; WGPUTextureView view = nullptr; };
    std::unordered_map<std::string, PreviewTex> preview_cache_;
    WGPUTextureView preview_view(const std::string& slug);   // load-or-get; null if there is no preview

    vivid::VisualGraph* vg_ = nullptr;
    const vivid::ShaderLibrary* shaders_ = nullptr;   // ADR-0016 (optional; chooser badge)
    std::set<std::string> quarantined_;               // ADR-0018 (disabled ops shown greyed)
    vivid::EditGateway* edit_gateway_ = nullptr;      // ADR-0017 (optional; UI edit capture)
    void note_edit_(const char* label, const char* key = "");   // fold a graph edit into the gesture

    // ADR-0053 A2: the port position of source node `ni`'s output row `oi` (right edge, row-centre).
    void source_out_port(int ni, int oi, float& px, float& py) const;
    int  find_source_node(const std::string& src) const;             // node index whose ANY output == src, -1
    bool find_source_output(const std::string& src, int& ni, int& oi) const;  // node+output for src; false if none
    void size_source_node(SourceNode& n) const;                      // recompute w/h from outs.size()
    // ADR-0053 Phase A: ensure a visible ENTITY source node exists for `src` (idempotent, no undo note —
    // safe on load + programmatic mapping). Parses master.*/transport.* -> one "Master" node, track_<id>.*
    // -> one "Track <id>" node (each with its full default output set), else a single-output "Other" node.
    // The single chokepoint that makes every mapping's source appear on the canvas.
    void ensure_source_node(const std::string& src);

    void sync_op_pos();
    int  op_index_of_id(int id) const;   // ADR-0033 P1: stable op-node id -> current index, -1 if gone (O(n))
    void resync_sel_op_();               // ADR-0033 P1: sel_op_ = index of sel_.primary() (-1 if empty)
    CardPorts card_ports(int i) const;   // shared card port-row layout (ADR-0023)
    void op_node_rect(int i, float& x, float& y, float& w, float& h) const;
    bool op_in_port(int i, int port, float& px, float& py) const;  // texture input port `port`; false if out of range
    bool op_out_port(int i, float& px, float& py) const;  // false if op has no output (= output port 0)
    bool op_out_port(int i, int port, float& px, float& py) const;  // a specific OUTPUT port (multi-output)
    // ADR-0053 Phase B: value-lane ordinal of op i's OUTPUT port `port` (-1 if not a value lane), and the
    // inverse (screen position of the output stub carrying value `lane`) — the control-edge port bridge.
    int  op_out_value_lane(int i, int port) const;
    bool op_out_port_of_lane(int i, int lane, float& px, float& py) const;
    void set_op_input_port(int node, int port, int src);  // wire src -> node's texture input `port` (-1 clears)
    int  first_node_of(const std::string& op_type) const; // -1 if none
    // Per-node param port: position of node_idx's local param row. False if out of range.
    bool param_port(int node_idx, int local, float& px, float& py) const;
    bool nearest_param(double x, double y, double maxd, int& node_idx, int& local) const;
    int  nearest_op_in(double x, double y, double maxd, int& port) const; // node index (-1 none) + which input port
    int  nearest_op_out(double x, double y, double maxd, int& port) const;// node index (-1 none) + which OUTPUT port
};

}  // namespace vivid::ui
