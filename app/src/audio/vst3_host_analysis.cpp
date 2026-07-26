// ADR-0025 (vst3_host split): the analysis / publication READ surface, extracted verbatim from
// vst3_host.cpp. These are the cold, main-thread (frame) accessors over state the audio thread PUBLISHES
// each block — meters (level/transient/bands), the note-derived bridge scalars (pitch/velocity/gate), the
// polyphonic held-note set, the per-track/master spectrum ring (for the frame-side FFT), the per-node
// FFT-capture gate + its ring, and a modulator node's control-out. None of this renders audio or mutates
// graph topology; it only reads published atomics/rings (a torn read is a benign 1-frame blip). The audio
// thread's WRITERS of this state stay in vst3_host.cpp's render path. The public functions are the session
// C API (declared in vst3_host.h); the ring-copy helper is file-local and moved with them.
#include "audio/vst3_host_internal.h"

#include <algorithm>
#include <atomic>
#include <mutex>

namespace vivid::session {

float session_master_level(Session* s) { return s ? s->master.meter.level.load(std::memory_order_relaxed) : 0.f; }
float session_master_transient(Session* s) { return s ? s->master.meter.transient.load(std::memory_order_relaxed) : 0.f; }
float session_master_band(Session* s, int b) {
    if (!s) return 0.f;
    switch (b) { case 0: return s->master.meter.band_low.load(std::memory_order_relaxed);
                 case 1: return s->master.meter.band_mid.load(std::memory_order_relaxed);
                 case 2: return s->master.meter.band_high.load(std::memory_order_relaxed);
                 default: return 0.f; }
}
float session_track_level(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->meter.level.load(std::memory_order_relaxed) : 0.f;
}
float session_track_transient(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->meter.transient.load(std::memory_order_relaxed) : 0.f;
}
float session_track_band(Session* s, int t, int band) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0.f;
    Track& tr = *s->tracks[t];
    return band == 0 ? tr.meter.band_low.load(std::memory_order_relaxed)
         : band == 1 ? tr.meter.band_mid.load(std::memory_order_relaxed)
                     : tr.meter.band_high.load(std::memory_order_relaxed);
}
float session_track_note_pitch(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->note_pitch.load(std::memory_order_relaxed) : 0.f;
}
float session_track_note_velocity(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->note_vel.load(std::memory_order_relaxed) : 0.f;
}
float session_track_note_gate(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->note_gate.load(std::memory_order_relaxed) : 0.f;
}
int session_track_analysis_copy(Session* s, int t, float* out, int n) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !out || n <= 0) return 0;
    return s->tracks[t]->meter.an_ring.snapshot(out, n);   // atomic-slot ring (ADR-0029)
}
int session_master_analysis_copy(Session* s, float* out, int n) {
    if (!s || !out || n <= 0) return 0;
    return s->master.meter.an_ring.snapshot(out, n);
}
// A modulator/LFO node's latest 0..1 control output (published by run_modulator_step into ctl_pub,
// indexed by node index == out_buf). 0 for non-modulator nodes (they never write it). `i` is the node
// enumeration index (as node_scope/node_kind take).
float session_track_audio_graph_node_control_out(Session* s, int t, int i) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || i < 0 || i >= kGraphMaxNodes) return 0.f;
    Track& tr = *s->tracks[t];
    return tr.ctl_pub ? tr.ctl_pub[i].load(std::memory_order_relaxed) : 0.f;
}
// Set which of a track's audio-graph nodes to capture for FFT (bit i == node index i). UI thread:
// allocate the per-node ring on the first non-zero mask (before the release-store the RT thread reads),
// so the audio thread only ever touches a stable, fully-allocated buffer.
void session_set_track_node_analyze_mask(Session* s, int t, uint64_t mask) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return;
    Track& tr = *s->tracks[t];
    if (mask) tr.node_an.allocate(kGraphMaxNodes, kAnalysisN);   // once, before the mask release-store below
    tr.node_analyze_mask.store(mask, std::memory_order_release);
}
// Snapshot a watched node's recent samples (oldest→newest) for the frame-side FFT. 0 if the node isn't
// being captured (ring unallocated / bit clear / unknown id).
int session_track_node_analysis_copy(Session* s, int t, int node_id, float* out, int n) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !out || n <= 0) return 0;
    Track& tr = *s->tracks[t];
    if (!tr.node_an.allocated()) return 0;
    int idx;
    { std::lock_guard<std::mutex> lk(tr.gmtx); idx = tr.agraph.node_index(node_id); }
    if (idx < 0 || idx >= kGraphMaxNodes) return 0;
    return tr.node_an.snapshot(idx, out, n);
}
// Snapshot a track's currently-held notes (lock-free: read count then array; a torn read is benign).
int session_track_active_notes(Session* s, int t, ActiveNote* out, int max) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !out || max <= 0) return 0;
    const Track& tr = *s->tracks[t];
    const int n = std::min<int>(max, std::min<uint32_t>(tr.held_count_.load(std::memory_order_acquire), kMaxHeld));
    for (int i = 0; i < n; ++i) { out[i].pitch = tr.held_[i].pitch; out[i].vel = tr.held_[i].vel; }
    return n;
}

}  // namespace vivid::session
