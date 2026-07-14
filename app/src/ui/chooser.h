#pragma once
#include "ui/renderer_2d.h"

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

// One row. `id` is whatever the owner needs to spawn it (an operator type name, a plugin bundle
// path, …); `tag` is an owner-defined discriminator (e.g. native-op vs VST3 vs CLAP).
// `enabled == false` renders the row greyed and refuses to spawn it — used to show a capability the
// catalog has but the engine can't host yet (a CLAP note-effect before ADR-0015's note edges land),
// because hiding it would make the catalog lie.
struct ChooserEntry {
    std::string label;      // what the user reads + what ranking matches against
    std::string id;         // op type / bundle path
    std::string summary;    // one-line description, drawn under the label
    std::string badge;      // short right-aligned kind/format tag ("VST3", "CLAP", "FX", "INS")
    std::string hay;        // extra searchable metadata (keywords, vendor, class) — lowercased by build()
    int   tag = 0;          // owner-defined (see the audio/visual catalogs)
    bool  enabled = true;
    const float* accent = nullptr;   // optional badge color (a ui_style token); null = default
    std::string disabled_note;       // why it can't be spawned (drawn instead of the summary)
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
    static constexpr float kW = 320.f, kRowH = 30.f, kHdrH = 26.f;
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
