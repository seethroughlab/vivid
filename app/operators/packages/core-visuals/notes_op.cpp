// Core visual package operator: Notes — publishes a track's currently-held MIDI notes as a
// first-class NOTE-SET VALUE on a custom-ref output port, for the composable note pipeline
// (Notes -> Instancer -> ...). Like MeshLoad only PRODUCES geometry, Notes only produces the note
// stream; a downstream node decides how to draw it. This is the visible "Note node" that drives an
// instancer (or any note consumer) through a graph edge, rather than a monolithic op reading notes
// internally. It reads the engine's active-notes bus by track STABLE id (its `track` param) — so it
// follows its track across reorder/delete, like every other per-track bridge source.
//
// A source (no inputs). Its output is a VividNoteSet custom-ref, NOT a texture, so it has no visible
// thumbnail — the render happens downstream.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/note_geom.h"   // VividNoteSet, publish_notes
#include "operator_api/note_bus.h"    // vivid_track_active_notes

#include <array>
#include <cmath>
#include <vector>

struct NotesOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Notes";
    static constexpr const char* kDisplayName = "Notes";
    static constexpr const char* kSummary = "A track's live MIDI notes as a value — drives an Instancer (or any note consumer) through an edge.";
    static constexpr std::array<const char*, 3> kKeywords = {"notes", "midi", "source"};

    vivid::Param<float> track{"track", 0.f, 0.f, 127.f};   // which track's held notes, by STABLE id

    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&track); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(VIVID_CUSTOM_REF_PORT("notes", VIVID_PORT_OUTPUT, VividNoteSet));
    }

    void process_gpu(const VividGpuContext* c) override {
        const float* p = c->param_values;
        const int track_id = static_cast<int>(std::lround(p ? p[0] : track.value));   // stable id (the bus searches)
        set_.count = vivid_track_active_notes(track_id, notes_, VIVID_MAX_ACTIVE_NOTES);
        set_.notes = notes_;
        vivid::notes::publish_notes(c, 0, &set_);   // downstream reads it this frame
    }

private:
    VividActiveNote notes_[VIVID_MAX_ACTIVE_NOTES];   // owned; kept alive for the frame
    VividNoteSet    set_{ notes_, 0 };
};

VIVID_REGISTER(NotesOp)
