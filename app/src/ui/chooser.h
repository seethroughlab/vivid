#pragma once
#include "ui/renderer_2d.h"
#include "ui/graph_catalog.h"   // CatalogSpawn — the typed domain/kind/spawn descriptor (ADR-0023 step 5)
#include "operator_api/types.h" // VividOperatorRole (ADR-0046 role chip)

#include <string>
#include <vector>

// The Tab chooser: the ONE way to add a node to a graph. Type to filter, Enter to spawn — at the
// cursor, where you're already working.
//
// Generic over its entries so the visuals graph and the audio graph share one implementation
// (the same move node_canvas.h made for the two graph canvases). Each surface builds its own
// catalog of `ChooserEntry`; the widget owns filtering, ranking, keys, geometry and drawing, and
// hands back the chosen entry.
namespace vivid::ui {

// One row. `enabled == false` renders the row greyed and refuses to spawn it — used to show a
// capability the catalog has but the engine can't host yet (a CLAP note-effect before ADR-0015's note
// edges land), because hiding it would make the catalog lie.
//
// NODE catalogs (the two graph editors) carry the typed `spawn` descriptor (ADR-0023 step 5) and their
// spawn dispatcher switches on `spawn.kind`. `id`/`tag` remain owner-opaque fields the widget passes
// through untouched — still used by the non-catalog param pickers (`tag` = a param/enum index).
struct ChooserEntry {
    std::string label;      // what the user reads + what ranking matches against
    std::string id;         // owner-opaque (param pickers); node catalogs use `spawn` instead
    std::string summary;    // one-line description, drawn under the label
    std::string badge;      // short right-aligned kind/format tag ("VST3", "CLAP", "FX", "INS")
    std::string hay;        // extra searchable metadata (keywords, vendor, class) — lowercased by build()
    int   tag = 0;          // owner-opaque discriminator (param pickers use it as an index)
    bool  enabled = true;
    const float* accent = nullptr;   // optional badge color (a ui_style token); null = default
    std::string disabled_note;       // why it can't be spawned (drawn instead of the summary)
    CatalogSpawn spawn;              // ADR-0023 step 5: the typed domain/kind/spawn payload (node catalogs)
    // ADR-0046: composable-primitive vs recipe classification, shown as a right-aligned chip on the
    // summary line. DEFAULT => no chip (plugins, shaders, bridge/data rows). See ui/chooser_rank.h.
    VividOperatorRole role = VIVID_OP_ROLE_DEFAULT;
};

class Chooser {
public:
    using Entry = ChooserEntry;

    bool open() const { return open_; }
    // Open at the cursor (screen coords). `bounds` keeps the panel inside its graph region.
    void show(double sx, double sy, float bx0, float by0, float bx1, float by1);
    void hide() { open_ = false; }
    void set_entries(std::vector<ChooserEntry> entries);   // the catalog, rebuilt on each show()

    // Keys (the owner routes them while open() is true).
    void move(int dir);            // +1 down / -1 up
    void backspace();
    void type(unsigned int cp);    // a typed filter character

    // The selected entry, or nullptr when nothing matches / it is disabled. The caller spawns it.
    const ChooserEntry* confirm();
    // Mouse: a click at (x,y) either picks a row (returns it) or falls outside the panel (returns
    // nullptr and sets `dismissed`, so the caller can close + consume).
    const ChooserEntry* click(double x, double y, bool& dismissed);

    void draw(Renderer2D& r) const;   // drawn in the overlay pass (above node cards + thumbnails)

    // Where a spawned node should land: the cursor position the chooser was opened at.
    float spawn_x() const { return sx_; }
    float spawn_y() const { return sy_; }

private:
    static constexpr float kW = 440.f, kRowH = 30.f, kHdrH = 26.f;
    static constexpr int   kMaxRows = 9;
    void  rebuild();
    void  geom(float& px, float& py, float& w, float& h, int& vis, int& first) const;

    bool  open_ = false;
    std::string filter_;
    int   sel_ = 0;
    float sx_ = 0.f, sy_ = 0.f;                  // cursor at open = the spawn anchor
    float bx0_ = 0.f, by0_ = 0.f, bx1_ = 0.f, by1_ = 0.f;   // the graph region to stay inside
    std::vector<ChooserEntry> entries_;
    std::vector<int> hits_;                      // indices into entries_, best match first
};

}  // namespace vivid::ui
