// Multi-track session — N tracks, each a hosted VST3 instrument + per-scene MIDI
// clips, mixed (per-track gain) to the master output with bar-quantized launch.
// Built on classic's extracted host (vst3_host_common.h, anonymous namespace).
#include "vst3_host_common.h"
#include "vst3_host.h"
#include "midi/midi_clip.h"
#include "audio/sampler.h"
#include "audio/clip_dsp.h"                           // A2: per-clip warp stretcher (ClipDsp + process_clip)
#include "pluginterfaces/vst/ivstnoteexpression.h"   // kTuningTypeID / kBrightnessTypeID

#include <vector>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <utility>
#include <filesystem>
#include <dirent.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vivid::session {

struct Track {
    Vst3Handle*           handle = nullptr;
    std::string           name;
    int                   id = -1;   // stable identity (monotonic; survives reorders/deletes)
    std::vector<MidiClip> clips;          // one per scene
    ClipScheduler         sched;
    std::atomic<int>      active{0};
    std::atomic<int>      queued{-1};
    std::atomic<float>    gain{0.8f};
    std::atomic<float>    level{0.f};
    std::atomic<float>    transient{0.f};
    float                 tr_baseline = 0.f;  // onset detector baseline (audio thread)
    std::atomic<float>    band_low{0.f}, band_mid{0.f}, band_high{0.f};  // 3-band energy
    float                 flt_lo = 0.f, flt_hi = 0.f;  // one-pole crossover states
    std::vector<float>    bl, br;          // planar scratch
    std::vector<NoteEvent> nev;
    std::vector<ExprEvent> eev;            // per-note expression scratch (M3), pre-reserved
    uint64_t              steady = 0;
    std::vector<Vst3Handle*> effects;      // post-generator FX chain (audio working copy)
    std::vector<float>    fxl, fxr;         // effect I/O scratch
    // Thread-safe FX edits (mirror of the clip-edit pattern): the UI mutates
    // effects_edit; the audio thread copies it into `effects` when fx_gen bumps.
    std::mutex               fx_mtx;
    std::vector<Vst3Handle*> effects_edit;
    std::atomic<uint64_t>    fx_gen{0};
    uint64_t                 fx_gen_seen = 0;
    std::vector<Vst3Handle*> fx_retired;   // removed handles, freed at shutdown (no audio free)
    // Live MIDI editing: the UI edits edit_clips; the audio thread copies them
    // into `clips` element-wise (clip addresses stay stable) when edit_gen bumps.
    std::mutex            edit_mtx;
    std::vector<MidiClip> edit_clips;
    std::atomic<uint64_t> edit_gen{0};
    uint64_t              edit_gen_seen = 0;
    // Audio track: no plugin; per-scene samples played transport-locked. `aud_clips` is
    // sized to `scenes` (an empty Sampler = empty cell). Content edits (stash/place a
    // clip) happen on the UI thread under aud_mtx; the audio thread try_locks it around
    // render (skips a block on contention) — the UI critical section is an O(1) move.
    bool                  is_audio = false;
    std::vector<Sampler>  aud_clips;
    std::vector<std::unique_ptr<ClipDsp>> aud_dsp;   // A2: per-slot warp stretcher (null until warp on)
    std::mutex            aud_mtx;
    std::atomic<float>    aud_trim0[8];   // per-scene loop window (fractions)
    std::atomic<float>    aud_trim1[8];
};

// A loose clip in the session-level pool (lives outside the track grid). Holds either a
// MIDI clip or an audio clip (Sampler). UI-thread-only storage: the audio thread never
// reads `Session::pool`, so no edit-mirror is needed.
struct PoolClip { bool is_audio = false; MidiClip clip; Sampler audio; std::string name; };

struct Session {
    Vst3HostApp host;
    std::vector<PoolClip> pool;   // clips stashed outside the grid (browser sidebar; UI-thread-only)
    // `tracks` is the UI/main-thread-authoritative list + owner (every session_* accessor
    // indexes it). The audio thread NEVER touches it; it iterates `tracks_view`, refreshed
    // from `tracks_pub` via tracks_gen + try_lock — the same edit-mirror pattern as the
    // per-track FX list, lifted to the track list. Removed tracks move to `tracks_retired`
    // (kept alive so an in-flight audio block never sees a freed Track) and are freed at
    // shutdown. All three Track* vectors are reserved to kMaxTracks so the swap + pushes
    // never reallocate.
    std::vector<std::unique_ptr<Track>> tracks;
    std::vector<std::unique_ptr<Track>> tracks_retired;
    std::vector<Track*>   tracks_pub;     // UI-published snapshot (guarded by tracks_mtx)
    std::vector<Track*>   tracks_view;    // audio working copy (audio thread only)
    std::mutex            tracks_mtx;
    std::atomic<uint64_t> tracks_gen{0};
    uint64_t              tracks_gen_seen = 0;
    int       next_track_id = 0;   // monotonic source of stable per-track IDs
    int       scenes = 3;
    long long last_bar = -1;
    uint32_t  sample_rate = 0;
};

// Republish the current track membership for the audio thread (UI/main thread only).
// Call after any add/remove; the audio thread picks it up on its next block.
static void rebuild_track_view(Session* s) {
    std::lock_guard<std::mutex> lk(s->tracks_mtx);
    s->tracks_pub.clear();
    for (auto& tp : s->tracks) s->tracks_pub.push_back(tp.get());
    s->tracks_gen.fetch_add(1, std::memory_order_release);
}

static Vst3Handle* load_effect(const std::string& path, uint32_t sr, Vst3HostApp* host) {
    Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sr, std::string(), host, /*as_effect*/true);
    if (!h) return nullptr;
    if (h->processor->setProcessing(true) != kResultOk) {}
    h->processing = true;
    return h;
}

// Parse a loop's source tempo from its path (e.g. ".../140 BPM/..." or "112bpm").
static double parse_bpm(const std::string& path) {
    std::string p = path;
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (size_t pos = p.find("bpm"); pos != std::string::npos; pos = p.find("bpm", pos + 1)) {
        long i = static_cast<long>(pos) - 1;
        while (i >= 0 && p[i] == ' ') --i;
        long end = i;
        while (i >= 0 && std::isdigit(static_cast<unsigned char>(p[i]))) --i;
        if (end > i) {
            int bpm = std::atoi(p.substr(i + 1, end - i).c_str());
            if (bpm >= 40 && bpm <= 300) return bpm;
        }
    }
    return 0.0;
}

static bool name_has(const std::string& path, const char* lower_needle) {
    std::string p = path;
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p.find(lower_needle) != std::string::npos;
}

// An audio track keeps exactly `scenes` clip slots (an empty Sampler = empty cell), so
// stash/place/launch address any scene by a stable index.
static void pad_aud_clips(Track* t, int scenes) {
    if (static_cast<int>(t->aud_clips.size()) > scenes) t->aud_clips.resize(scenes);
    while (static_cast<int>(t->aud_clips.size()) < scenes) t->aud_clips.emplace_back();
}

static void list_vst3(const std::string& dir, std::vector<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".vst3") == 0)
            out.push_back(dir + "/" + n);
    }
    closedir(d);
}

static MidiClip transpose(const MidiClip& c, int semis) {
    MidiClip o = c;
    for (auto& n : o.notes) n.pitch += semis;
    return o;
}
// Three riffs (scene A/B/C); each track plays them transposed to a role.
static std::vector<MidiClip> base_patterns() {
    MidiClip a; a.length = 4.0; a.notes = {
        {62,0.0,0.5,.85f},{65,0.5,0.5,.70f},{69,1.0,0.5,.85f},{65,1.5,0.5,.70f},{74,2.0,1.0,.90f},{69,3.0,0.9,.75f} };
    MidiClip b; b.length = 4.0; b.notes = {
        {81,0.0,0.4,.80f},{84,1.0,0.4,.75f},{88,2.0,0.4,.80f},{86,3.0,0.9,.70f} };
    MidiClip c; c.length = 4.0; c.notes = {
        {38,0.0,0.4,.95f},{38,0.5,0.4,.70f},{41,1.0,0.4,.90f},{38,1.5,0.4,.70f},
        {36,2.0,0.4,.95f},{36,2.5,0.4,.70f},{43,3.0,0.4,.90f},{41,3.5,0.4,.70f} };
    return { a, b, c };
}

// GM drum map: 36 kick, 38 snare, 42 closed hat, 46 open hat.
static std::vector<MidiClip> drum_patterns() {
    MidiClip a; a.length = 4.0;  // straight backbeat
    a.notes = { {36,0.0,.2,.95f},{36,2.0,.2,.95f},{38,1.0,.2,.90f},{38,3.0,.2,.90f} };
    for (double t = 0; t < 4; t += 0.5) a.notes.push_back({42, t, 0.1, 0.55f});
    MidiClip b; b.length = 4.0;  // busier, 16th hats
    b.notes = { {36,0.0,.2,.95f},{36,1.5,.2,.85f},{36,2.0,.2,.95f},{36,2.75,.2,.80f},
                {38,1.0,.2,.90f},{38,3.0,.2,.90f} };
    for (double t = 0; t < 4; t += 0.25) b.notes.push_back({42, t, 0.08, 0.5f});
    MidiClip c; c.length = 4.0;  // half-time, open hats on the quarter
    c.notes = { {36,0.0,.2,.95f},{38,2.0,.2,.90f} };
    for (double t = 0; t < 4; t += 1.0) c.notes.push_back({46, t, 0.2, 0.5f});
    return { a, b, c };
}

enum TrackKind { kLead = 0, kBass = 1, kDrums = 2 };

static Track* make_track(Vst3Handle* h, const std::string& name, int kind) {
    auto* t = new Track();
    t->handle = h;
    t->name = name;
    if (kind == kDrums) {
        for (auto& p : drum_patterns()) t->clips.push_back(p);
    } else {
        const int off = (kind == kBass) ? -12 : 0;  // lead at pitch, bass an octave down
        for (auto& bp : base_patterns()) t->clips.push_back(transpose(bp, off));
    }
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(64);
t->eev.reserve(256);
    t->edit_clips = t->clips;  // editor's mirror starts equal to the live clips
    t->effects.reserve(16); t->effects_edit.reserve(16);  // avoid audio-thread realloc
    return t;
}

// A dynamically-added instrument track: empty clips (the user authors them) across all
// scenes, so set_clip/launch work immediately.
static Track* make_instrument_track(Vst3Handle* h, const std::string& name, int scenes) {
    auto* t = new Track();
    t->handle = h;
    t->name = name;
    for (int i = 0; i < scenes; ++i) { MidiClip c; c.length = 4.0; t->clips.push_back(c); }
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(64);
t->eev.reserve(256);
    t->edit_clips = t->clips;
    t->effects.reserve(16); t->effects_edit.reserve(16);
    return t;
}

// Load the first plugin matching a role's preference list (never "atoms" — no
// license here), skipping anything that isn't an instrument with a MIDI input.
static Vst3Handle* load_role(const std::vector<std::string>& bundles,
                             const char* const* prefer, uint32_t sr,
                             Vst3HostApp* host, std::string& out_name) {
    for (int p = 0; prefer[p]; ++p) {
        for (const auto& path : bundles) {
            if (name_has(path, "atoms")) continue;
            if (!name_has(path, prefer[p])) continue;
            Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sr, std::string(), host);
            if (!h) continue;
            if (!(h->component && h->component->getBusCount(kEvent, kInput) > 0)) { h->destroy(); delete h; continue; }
            if (h->processor->setProcessing(true) != kResultOk) {}
            h->processing = true;
            out_name = h->plugin_name.empty() ? path : h->plugin_name;
            return h;
        }
    }
    return nullptr;
}

Session* session_create(uint32_t sample_rate) {
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);

    // Role-based assignment: lead synth, bass synth, drums. "" matches any
    // remaining instrument as a last resort; drums has no synth fallback.
    struct RoleSpec { const char* prefer[6]; int kind; };
    static const RoleSpec kRoles[] = {
        { { "pigments", "vital", "serum", "", nullptr }, kLead },
        { { "serum", "vital", "pigments", "", nullptr }, kBass },
        { { "ezdrummer", "drumcomputer", "battery", "drum", nullptr }, kDrums },
    };

    auto* s = new Session();
    s->sample_rate = sample_rate;
    s->tracks.reserve(kMaxTracks);
    s->tracks_pub.reserve(kMaxTracks);
    s->tracks_view.reserve(kMaxTracks);
    for (const auto& role : kRoles) {
        std::string name;
        Vst3Handle* h = load_role(bundles, role.prefer, sample_rate, &s->host, name);
        if (!h) { std::fprintf(stderr, "[Session] role kind %d unfilled\n", role.kind); continue; }
        s->tracks.emplace_back(make_track(h, name, role.kind));
        s->tracks.back()->id = s->next_track_id++;
        std::fprintf(stderr, "[Session] track %zu: %s\n", s->tracks.size() - 1, name.c_str());
    }

    // Auto-load one audio effect onto the lead track to prove the FX chain.
    if (!s->tracks.empty()) {
        static const char* fx_prefer[] = { "yak", "chowtape", "chow", "portal", "infiltrator", nullptr };
        for (int p = 0; fx_prefer[p]; ++p) {
            std::string fxpath;
            for (const auto& b : bundles) if (!name_has(b, "atoms") && name_has(b, fx_prefer[p])) { fxpath = b; break; }
            if (fxpath.empty()) continue;
            if (Vst3Handle* fx = load_effect(fxpath, sample_rate, &s->host)) {
                s->tracks[0]->effects_edit.push_back(fx);
                s->tracks[0]->effects = s->tracks[0]->effects_edit;  // active immediately (pre-audio)
                std::fprintf(stderr, "[Session] track 0 effect: %s\n",
                             fx->plugin_name.empty() ? fxpath.c_str() : fx->plugin_name.c_str());
                break;
            }
        }
    }

    // A built-in audio (sampler) track. Loads 3 real loops at distinct source
    // tempos (warped to the session) from the Dan Mayo library if present, else
    // falls back to the procedural demo loops.
    {
        auto at = std::make_unique<Track>();
        at->is_audio = true;
        at->name = "Audio";
        at->gain.store(0.7f, std::memory_order_relaxed);
        for (int i = 0; i < 8; ++i) { at->aud_trim0[i].store(0.f); at->aud_trim1[i].store(1.f); }

        namespace fs = std::filesystem;
        std::vector<std::pair<std::string, double>> loops;  // (path, source bpm)
        if (const char* home = std::getenv("HOME")) {
            std::error_code ec;
            fs::path base = fs::path(home) / "Music/Ableton/User Library/Samples/Dan Mayo";
            if (fs::exists(base, ec)) {
                for (auto it = fs::recursive_directory_iterator(base, ec);
                     it != fs::recursive_directory_iterator(); it.increment(ec)) {
                    if (ec) break;
                    if (!it->is_regular_file(ec)) continue;
                    const std::string sp = it->path().string();
                    if (sp.size() < 4 || (sp.compare(sp.size() - 4, 4, ".wav") != 0
                                          && sp.compare(sp.size() - 4, 4, ".WAV") != 0)) continue;
                    const double b = parse_bpm(sp);
                    if (b > 0) loops.emplace_back(sp, b);
                }
            }
        }
        if (!loops.empty()) {
            std::sort(loops.begin(), loops.end(), [](auto& a, auto& b) { return a.second < b.second; });
            std::vector<double> bpms;
            for (auto& l : loops) if (bpms.empty() || bpms.back() != l.second) bpms.push_back(l.second);
            const double pick[3] = { bpms.front(), bpms[bpms.size() / 2], bpms.back() };
            for (double tb : pick) {
                for (auto& l : loops) if (l.second == tb) {
                    Sampler smp;
                    if (sampler_load_wav(l.first, sample_rate, tb, smp)) at->aud_clips.push_back(std::move(smp));
                    break;
                }
            }
        }
        if (at->aud_clips.empty()) {  // no library found
            at->aud_clips.push_back(gen_sub_pulse(sample_rate, 124.0));
            at->aud_clips.push_back(gen_noise_sweep(sample_rate, 124.0));
            at->aud_clips.push_back(gen_bell_loop(sample_rate, 124.0));
        }
        pad_aud_clips(at.get(), s->scenes);
        at->active.store(-1, std::memory_order_relaxed);  // start stopped
        s->tracks.emplace_back(std::move(at));
        s->tracks.back()->id = s->next_track_id++;
        std::fprintf(stderr, "[Session] track %zu: Audio (sampler, %zu loops)\n",
                     s->tracks.size() - 1, s->tracks.back()->aud_clips.size());
    }

    if (s->tracks.empty()) { delete s; return nullptr; }
    rebuild_track_view(s);   // publish the initial set to the audio thread
    std::fprintf(stderr, "[Session] %zu tracks, %d scenes\n", s->tracks.size(), s->scenes);
    return s;
}

int  session_track_count(Session* s) { return s ? static_cast<int>(s->tracks.size()) : 0; }
int  session_scene_count(Session* s) { return s ? s->scenes : 0; }
const char* session_track_name(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->name.c_str() : "";
}
int session_track_id(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->id : -1;
}
void session_set_track_id(Session* s, int t, int id) {   // load-time restore of a saved id
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return;
    s->tracks[t]->id = id;
    if (id >= s->next_track_id) s->next_track_id = id + 1;   // keep new ids from colliding
}
int  session_active_clip(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->active.load(std::memory_order_relaxed) : -1;
}
int  session_queued_clip(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->queued.load(std::memory_order_relaxed) : -1;
}
void session_launch_clip(Session* s, int t, int scene) {
    if (s && t >= 0 && t < static_cast<int>(s->tracks.size()) && scene >= 0 && scene < s->scenes)
        s->tracks[t]->queued.store(scene, std::memory_order_relaxed);
}
void session_launch_scene(Session* s, int scene) {
    if (!s || scene < 0 || scene >= s->scenes) return;
    for (auto& tp : s->tracks) tp->queued.store(scene, std::memory_order_relaxed);
}
float session_track_gain(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->gain.load(std::memory_order_relaxed) : 0.f;
}
void session_set_track_gain(Session* s, int t, float g) {
    if (s && t >= 0 && t < static_cast<int>(s->tracks.size())) s->tracks[t]->gain.store(g, std::memory_order_relaxed);
}
float session_track_level(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->level.load(std::memory_order_relaxed) : 0.f;
}
float session_track_transient(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->transient.load(std::memory_order_relaxed) : 0.f;
}
float session_track_band(Session* s, int t, int band) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0.f;
    Track& tr = *s->tracks[t];
    return band == 0 ? tr.band_low.load(std::memory_order_relaxed)
         : band == 1 ? tr.band_mid.load(std::memory_order_relaxed)
                     : tr.band_high.load(std::memory_order_relaxed);
}
void* session_track_controller(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->handle) ? s->tracks[t]->handle->controller : nullptr;
}
bool session_track_is_audio(Session* s, int t) {
    return s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->is_audio;
}
std::string session_get_track_state(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !s->tracks[t]->handle) return {};
    return vst3_save_state(s->tracks[t]->handle);
}
void session_set_track_state(Session* s, int t, const std::string& state) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !s->tracks[t]->handle || state.empty()) return;
    vst3_load_state(s->tracks[t]->handle, state);
}
int session_audio_clip_bpm(Session* s, int t, int sc) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return 0;
    Track& tr = *s->tracks[t];
    if (!tr.is_audio || sc < 0 || sc >= static_cast<int>(tr.aud_clips.size())) return 0;
    return static_cast<int>(std::lround(tr.aud_clips[sc].src_bpm));
}
static bool aud_valid(Session* s, int t, int sc) {
    return s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->is_audio
           && sc >= 0 && sc < static_cast<int>(s->tracks[t]->aud_clips.size());
}
int session_audio_waveform(Session* s, int t, int sc, float* out, int n) {
    if (!aud_valid(s, t, sc) || !out || n <= 0) return 0;
    const Sampler& smp = s->tracks[t]->aud_clips[sc];
    if (!smp.ok()) return 0;
    const size_t N = smp.L.size();
    for (int i = 0; i < n; ++i) {
        const size_t a = N * static_cast<size_t>(i) / n, b = N * static_cast<size_t>(i + 1) / n;
        float peak = 0.f;
        for (size_t j = a; j < b && j < N; ++j) peak = std::max(peak, std::fabs(smp.L[j]));
        out[i] = peak;
    }
    return n;
}
void session_get_audio_trim(Session* s, int t, int sc, float* t0, float* t1) {
    if (!aud_valid(s, t, sc)) { if (t0) *t0 = 0.f; if (t1) *t1 = 1.f; return; }
    if (t0) *t0 = s->tracks[t]->aud_trim0[sc].load(std::memory_order_relaxed);
    if (t1) *t1 = s->tracks[t]->aud_trim1[sc].load(std::memory_order_relaxed);
}
void session_set_audio_trim(Session* s, int t, int sc, float t0, float t1) {
    if (!aud_valid(s, t, sc)) return;
    s->tracks[t]->aud_trim0[sc].store(std::min(std::max(t0, 0.f), 1.f), std::memory_order_relaxed);
    s->tracks[t]->aud_trim1[sc].store(std::min(std::max(t1, 0.f), 1.f), std::memory_order_relaxed);
}

// --- audio-clip warp/shaping (A2) — UI/main thread; writes are guarded by aud_mtx so the
// audio thread reads a consistent clip. Enabling warp builds+inits the stretcher OFF the
// lock (heavy) and swaps it in under the lock (short critical section). ---
void session_set_audio_warp(Session* s, int t, int sc, int enabled, int mode) {
    if (!aud_valid(s, t, sc)) return;
    Track& tr = *s->tracks[t];
    std::unique_ptr<ClipDsp> fresh;
    if (enabled) { fresh = std::make_unique<ClipDsp>(); fresh->init(s->sample_rate > 0 ? s->sample_rate : 48000); }
    std::lock_guard<std::mutex> lk(tr.aud_mtx);
    if (tr.aud_dsp.size() < tr.aud_clips.size()) tr.aud_dsp.resize(tr.aud_clips.size());
    tr.aud_clips[sc].warp_enabled = enabled != 0;
    tr.aud_clips[sc].warp_mode = static_cast<WarpMode>(std::clamp(mode, 0, 2));
    if (enabled) tr.aud_dsp[sc] = std::move(fresh);
}
int session_get_audio_warp(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return -1;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    return c.warp_enabled ? static_cast<int>(c.warp_mode) : -1;
}
void session_set_audio_pitch(Session* s, int t, int sc, float semitones) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    s->tracks[t]->aud_clips[sc].pitch_semitones = std::clamp(semitones, -48.f, 48.f);
}
float session_get_audio_pitch(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return 0.f;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    return s->tracks[t]->aud_clips[sc].pitch_semitones;
}
void session_set_audio_gain(Session* s, int t, int sc, float gain) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    s->tracks[t]->aud_clips[sc].gain = std::clamp(gain, 0.f, 4.f);
}
float session_get_audio_gain(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return 1.f;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    return s->tracks[t]->aud_clips[sc].gain;
}
void session_set_audio_reverse(Session* s, int t, int sc, int on) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    s->tracks[t]->aud_clips[sc].reverse = on != 0;
}
int session_get_audio_reverse(Session* s, int t, int sc) {
    if (!aud_valid(s, t, sc)) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    return s->tracks[t]->aud_clips[sc].reverse ? 1 : 0;
}
void session_set_audio_fades(Session* s, int t, int sc, float in_ms, float out_ms, float xfade_ms) {
    if (!aud_valid(s, t, sc)) return;
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    auto& c = s->tracks[t]->aud_clips[sc];
    c.fade_in_ms = std::max(0.f, in_ms); c.fade_out_ms = std::max(0.f, out_ms); c.loop_crossfade_ms = std::max(0.f, xfade_ms);
}
void session_get_audio_fades(Session* s, int t, int sc, float* in_ms, float* out_ms, float* xfade_ms) {
    if (!aud_valid(s, t, sc)) { if (in_ms) *in_ms = 0; if (out_ms) *out_ms = 0; if (xfade_ms) *xfade_ms = 0; return; }
    std::lock_guard<std::mutex> lk(s->tracks[t]->aud_mtx);
    const auto& c = s->tracks[t]->aud_clips[sc];
    if (in_ms) *in_ms = c.fade_in_ms; if (out_ms) *out_ms = c.fade_out_ms; if (xfade_ms) *xfade_ms = c.loop_crossfade_ms;
}

static bool clip_valid(Session* s, int t, int sc) {
    return s && t >= 0 && t < static_cast<int>(s->tracks.size())
           && sc >= 0 && sc < static_cast<int>(s->tracks[t]->edit_clips.size());
}
int session_clip_note_count(Session* s, int t, int sc) {
    if (!clip_valid(s, t, sc)) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    return static_cast<int>(s->tracks[t]->edit_clips[sc].notes.size());
}
int session_get_clip(Session* s, int t, int sc, ClipNote* out, int max) {
    if (!clip_valid(s, t, sc) || !out || max <= 0) return 0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    const auto& notes = s->tracks[t]->edit_clips[sc].notes;
    const int n = std::min(static_cast<int>(notes.size()), max);
    for (int i = 0; i < n; ++i) out[i] = notes[i];
    return n;
}
double session_clip_length(Session* s, int t, int sc) {
    if (!clip_valid(s, t, sc)) return 0.0;
    std::lock_guard<std::mutex> lk(s->tracks[t]->edit_mtx);
    return s->tracks[t]->edit_clips[sc].length;
}
void session_set_clip(Session* s, int t, int sc, const ClipNote* notes, int n, double length) {
    if (!clip_valid(s, t, sc)) return;
    Track& tr = *s->tracks[t];
    {
        std::lock_guard<std::mutex> lk(tr.edit_mtx);
        tr.edit_clips[sc].notes.assign(notes, notes + (n > 0 ? n : 0));
        tr.edit_clips[sc].length = length > 0 ? length : tr.edit_clips[sc].length;
    }
    tr.edit_gen.fetch_add(1, std::memory_order_release);
}

// --- Clip pool (loose clips outside the grid) — UI/main thread only. ---
static bool pool_valid(Session* s, int i) { return s && i >= 0 && i < static_cast<int>(s->pool.size()); }
int session_pool_count(Session* s) { return s ? static_cast<int>(s->pool.size()) : 0; }
double session_pool_length(Session* s, int i) {
    if (!pool_valid(s, i)) return 0.0;
    return s->pool[i].is_audio ? s->pool[i].audio.loop_beats : s->pool[i].clip.length;
}
const char* session_pool_name(Session* s, int i) { return pool_valid(s, i) ? s->pool[i].name.c_str() : ""; }
int session_pool_get(Session* s, int i, ClipNote* out, int max) {
    if (!pool_valid(s, i) || !out || max <= 0) return 0;
    const auto& notes = s->pool[i].clip.notes;
    const int n = std::min(static_cast<int>(notes.size()), max);
    for (int k = 0; k < n; ++k) out[k] = notes[k];
    return n;
}
int session_pool_add(Session* s, const ClipNote* notes, int n, double length, const char* name) {
    if (!s) return -1;
    PoolClip pc;
    if (notes && n > 0) pc.clip.notes.assign(notes, notes + n);
    pc.clip.length = length > 0 ? length : 4.0;
    pc.name = name ? name : "";
    s->pool.push_back(std::move(pc));
    return static_cast<int>(s->pool.size()) - 1;
}
void session_pool_remove(Session* s, int i) { if (pool_valid(s, i)) s->pool.erase(s->pool.begin() + i); }
void session_pool_clear(Session* s) { if (s) s->pool.clear(); }

// --- Audio clips in the pool (Samplers). Mirrors the MIDI pool; stash = MOVE. ---
static int sampler_waveform(const Sampler& smp, float* out, int n) {
    if (!smp.ok() || !out || n <= 0) return 0;
    const size_t N = smp.L.size();
    for (int i = 0; i < n; ++i) {
        const size_t a = N * static_cast<size_t>(i) / n, b = N * static_cast<size_t>(i + 1) / n;
        float peak = 0.f;
        for (size_t j = a; j < b && j < N; ++j) peak = std::max(peak, std::fabs(smp.L[j]));
        out[i] = peak;
    }
    return n;
}
bool session_pool_is_audio(Session* s, int i) { return pool_valid(s, i) && s->pool[i].is_audio; }
int  session_pool_audio_bpm(Session* s, int i) {
    return (pool_valid(s, i) && s->pool[i].is_audio) ? static_cast<int>(std::lround(s->pool[i].audio.src_bpm)) : 0;
}
int  session_pool_audio_waveform(Session* s, int i, float* out, int n) {
    return (pool_valid(s, i) && s->pool[i].is_audio) ? sampler_waveform(s->pool[i].audio, out, n) : 0;
}
// MOVE an audio grid clip into the pool: the source cell is cleared (under aud_mtx so the
// audio thread never sees a torn Sampler). Returns the new pool index, or -1.
int session_pool_stash_audio(Session* s, int t, int sc, const char* name) {
    if (!aud_valid(s, t, sc)) return -1;
    Track& tr = *s->tracks[t];
    if (!tr.aud_clips[sc].ok()) return -1;   // empty cell — nothing to stash
    PoolClip pc; pc.is_audio = true;
    {
        std::lock_guard<std::mutex> lk(tr.aud_mtx);
        pc.audio = std::move(tr.aud_clips[sc]);   // O(1) move out
        tr.aud_clips[sc] = Sampler{};             // leave an empty cell
    }
    pc.name = name ? name : "";
    s->pool.push_back(std::move(pc));
    return static_cast<int>(s->pool.size()) - 1;
}
// Copy a pooled audio clip into an audio grid cell (under aud_mtx). The pool keeps its copy.
bool session_pool_place_audio(Session* s, int i, int t, int sc) {
    if (!pool_valid(s, i) || !s->pool[i].is_audio || !aud_valid(s, t, sc)) return false;
    Track& tr = *s->tracks[t];
    Sampler copy = s->pool[i].audio;   // copy the PCM on the UI thread before locking
    {
        std::lock_guard<std::mutex> lk(tr.aud_mtx);
        tr.aud_clips[sc] = std::move(copy);
    }
    tr.aud_trim0[sc].store(0.f, std::memory_order_relaxed);
    tr.aud_trim1[sc].store(1.f, std::memory_order_relaxed);
    return true;
}

// Drain a device's pending UI parameter changes into its ParamChanges block.
static void drain_params(Vst3Handle* h, Vst3ParamChanges& pc) {
    ParamMsg m;
    while (h->param_q.pop(m)) {
        int32 idx = 0;
        IParamValueQueue* q = pc.addParameterData(m.id, idx);
        if (q) { int32 pt = 0; q->addPoint(0, m.value, pt); }
    }
}

// Note on/off + per-note expression. Note events are added first so a same-offset
// expression for a just-started note never precedes its note-on (VST3 wants the list
// sorted; continuing-note expression is at offset 0 with its note-on in a prior block).
// Axis mapping: bend -> kTuningTypeID (±120 semis, norm = semis/240 + 0.5), timbre ->
// kBrightnessTypeID (0..1), pressure -> per-note PolyPressureEvent (0..1).
static void emit_vst3(Vst3EventList& events, const std::vector<NoteEvent>& nev,
                      const std::vector<ExprEvent>& eev) {
    for (const NoteEvent& ne : nev) {
        Event e{};
        e.sampleOffset = static_cast<int32>(ne.sample_offset);
        e.busIndex = 0;
        if (ne.on) {
            e.type = Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<int16>(ne.pitch);
            e.noteOn.velocity = ne.vel;
            e.noteOn.noteId = ne.note_id;
            e.noteOn.channel = 0;
            e.noteOn.tuning = ne.tuning;   // semitone offset for a click-free bent start
        } else {
            e.type = Event::kNoteOffEvent;
            e.noteOff.pitch = static_cast<int16>(ne.pitch);
            e.noteOff.velocity = 0.f;
            e.noteOff.noteId = ne.note_id;
            e.noteOff.channel = 0;
        }
        events.addEvent(e);
    }
    for (const ExprEvent& xe : eev) {
        Event e{};
        e.sampleOffset = static_cast<int32>(xe.sample_offset);
        e.busIndex = 0;
        if (xe.axis == vivid::session::AXIS_PRESSURE) {
            e.type = Event::kPolyPressureEvent;
            e.polyPressure.channel = 0;
            e.polyPressure.pitch = static_cast<int16>(xe.pitch);
            e.polyPressure.pressure = std::clamp(xe.value, 0.f, 1.f);
            e.polyPressure.noteId = xe.note_id;
        } else {
            e.type = Event::kNoteExpressionValueEvent;
            e.noteExpressionValue.noteId = xe.note_id;
            if (xe.axis == vivid::session::AXIS_BEND) {
                e.noteExpressionValue.typeId = kTuningTypeID;
                e.noteExpressionValue.value = std::clamp(xe.value / 240.0 + 0.5, 0.0, 1.0);
            } else {  // AXIS_TIMBRE
                e.noteExpressionValue.typeId = kBrightnessTypeID;
                e.noteExpressionValue.value = std::clamp(static_cast<double>(xe.value), 0.0, 1.0);
            }
        }
        events.addEvent(e);
    }
}

bool session_process(Session* s, float* out, uint32_t frames, uint32_t sample_rate,
                     double bpm, double beats, uint32_t beats_per_bar,
                     bool playing, bool release_all) {
    if (!s) return false;
    std::memset(out, 0, sizeof(float) * 2 * frames);

    // Refresh the audio-thread track view if the UI added/removed a track (cheap gen-
    // counter fast-path; the copy is into reserved capacity, so no allocation). On a
    // contended block we keep the previous view and retry next block.
    if (s->tracks_gen.load(std::memory_order_acquire) != s->tracks_gen_seen) {
        if (s->tracks_mtx.try_lock()) {
            s->tracks_view = s->tracks_pub;
            s->tracks_gen_seen = s->tracks_gen.load(std::memory_order_acquire);
            s->tracks_mtx.unlock();
        }
    }
    if (s->tracks_view.empty()) return false;

    const uint32_t bpb = beats_per_bar ? beats_per_bar : 4;
    const long long bar = static_cast<long long>(std::floor(beats / bpb));
    const bool new_bar = bar != s->last_bar;
    s->last_bar = bar;
    const double delta = frames * (bpm / 60.0) / (sample_rate > 0 ? sample_rate : 48000);

    bool any = false;
    for (Track* tp : s->tracks_view) {
        Track& t = *tp;
        if (!t.is_audio && (!t.handle || !t.handle->processing)) continue;
        any = true;

        // Apply pending clip edits (element-wise so &clips[sc] — and the
        // scheduler's clip pointer — stay valid). Only runs after a user edit.
        if (t.edit_gen.load(std::memory_order_acquire) != t.edit_gen_seen) {
            if (t.edit_mtx.try_lock()) {
                const size_t ns = std::min(t.clips.size(), t.edit_clips.size());
                for (size_t sc = 0; sc < ns; ++sc) {
                    t.clips[sc].notes  = t.edit_clips[sc].notes;
                    t.clips[sc].length = t.edit_clips[sc].length;
                }
                // notes[] was re-assigned (may have reallocated) — the scheduler's
                // active[].src pointers now dangle. Null them (note-offs still fire).
                t.sched.invalidate_active_src();
                t.edit_gen_seen = t.edit_gen.load(std::memory_order_acquire);
                t.edit_mtx.unlock();
            }
        }
        // Apply pending FX-chain edits (copy the UI's pointer list into the working
        // one; reserved capacity avoids a realloc). Only runs after an add/remove.
        if (t.fx_gen.load(std::memory_order_acquire) != t.fx_gen_seen) {
            if (t.fx_mtx.try_lock()) {
                t.effects = t.effects_edit;
                t.fx_gen_seen = t.fx_gen.load(std::memory_order_acquire);
                t.fx_mtx.unlock();
            }
        }
        if (t.bl.size() < frames) { t.bl.resize(frames); t.br.resize(frames); }
        float* L = t.bl.data(); float* R = t.br.data();
        std::memset(L, 0, frames * sizeof(float));
        std::memset(R, 0, frames * sizeof(float));

        VividAudioContext ctx{};   // shared by the instrument and the effect chain
        ctx.sample_rate = sample_rate;
        ctx.metronome_bpm = static_cast<float>(bpm);
        ctx.metronome_beats_per_bar = bpb;
        ctx.metronome_beats_elapsed = beats;

        if (t.is_audio) {
            // Bar-quantized scene switch, then render the active sample loop.
            if (new_bar) {
                const int q = t.queued.load(std::memory_order_relaxed);
                if (q >= 0 && q != t.active.load(std::memory_order_relaxed)) t.active.store(q, std::memory_order_relaxed);
                if (q >= 0) t.queued.store(-1, std::memory_order_relaxed);
            }
            const int sc = t.active.load(std::memory_order_relaxed);
            // try_lock guards against a concurrent UI stash/place swapping this slot; on
            // contention we simply skip this block (the UI's move is O(1), so it's rare).
            if (playing && sc >= 0 && t.aud_mtx.try_lock()) {
                if (sc < static_cast<int>(t.aud_clips.size()) && t.aud_clips[sc].ok()) {
                    const float tr0 = t.aud_trim0[sc].load(std::memory_order_relaxed);
                    const float tr1 = t.aud_trim1[sc].load(std::memory_order_relaxed);
                    ClipDsp* d = (sc < static_cast<int>(t.aud_dsp.size())) ? t.aud_dsp[sc].get() : nullptr;
                    if (d && d->ready)  // warp enabled + stretcher ready -> pitch-preserving path
                        process_clip(t.aud_clips[sc], *d, beats, delta, frames, sample_rate, L, R, tr0, tr1);
                    else
                        t.aud_clips[sc].render(beats, delta, frames, L, R, tr0, tr1);
                }
                t.aud_mtx.unlock();
            }
        } else {
            Vst3EventList events;
            if (new_bar) {
                const int q = t.queued.load(std::memory_order_relaxed);
                if (q >= 0 && q != t.active.load(std::memory_order_relaxed) && q < static_cast<int>(t.clips.size())) {
                    t.nev.clear(); t.eev.clear(); t.sched.flush(t.nev); emit_vst3(events, t.nev, t.eev);
                    t.sched.reset(&t.clips[q]);
                    t.active.store(q, std::memory_order_relaxed);
                }
                if (q >= 0) t.queued.store(-1, std::memory_order_relaxed);
            }
            t.nev.clear(); t.eev.clear();
            if (release_all)    t.sched.flush(t.nev);                            // play->stop edge: release held notes
            else if (playing)   t.sched.emit(beats, delta, frames, t.nev, t.eev);  // paused: emit nothing (tails still ring)
            emit_vst3(events, t.nev, t.eev);

            float* ch[2] = { L, R };
            AudioBusBuffers ob{}; ob.channelBuffers32 = ch; ob.numChannels = 2; ob.silenceFlags = 0;
            Vst3ParamChanges pc; pc.clear();
            drain_params(t.handle, pc);
            ProcessContext pctx = vst3_build_process_context(&ctx, t.steady);
            ProcessData data{};
            data.processMode = kRealtime; data.symbolicSampleSize = kSample32;
            data.numSamples = static_cast<int32>(frames); data.numInputs = 0; data.numOutputs = 1;
            data.inputs = nullptr; data.outputs = &ob;
            data.inputEvents = &events; data.inputParameterChanges = &pc; data.processContext = &pctx;
            t.handle->processor->process(data);
        }

        // FX chain: process L/R through each effect in series (audio in -> out).
        for (Vst3Handle* fx : t.effects) {
            if (!fx || !fx->processing) continue;
            if (t.fxl.size() < frames) { t.fxl.resize(frames); t.fxr.resize(frames); }
            float* oL = t.fxl.data(); float* oR = t.fxr.data();
            float* inCh[2] = { L, R }; float* outCh[2] = { oL, oR };
            AudioBusBuffers ib{}; ib.channelBuffers32 = inCh;  ib.numChannels = 2; ib.silenceFlags = 0;
            AudioBusBuffers fob{}; fob.channelBuffers32 = outCh; fob.numChannels = 2; fob.silenceFlags = 0;
            Vst3ParamChanges fpc; fpc.clear();
            drain_params(fx, fpc);
            ProcessContext fpctx = vst3_build_process_context(&ctx, t.steady);
            ProcessData fd{};
            fd.processMode = kRealtime; fd.symbolicSampleSize = kSample32;
            fd.numSamples = static_cast<int32>(frames);
            fd.numInputs = 1; fd.inputs = &ib;
            fd.numOutputs = 1; fd.outputs = &fob;
            fd.inputEvents = nullptr; fd.inputParameterChanges = &fpc; fd.processContext = &fpctx;
            fx->processor->process(fd);
            std::memcpy(L, oL, frames * sizeof(float));
            std::memcpy(R, oR, frames * sizeof(float));
        }
        t.steady += frames;

        const float g = t.gain.load(std::memory_order_relaxed);
        const float sr = static_cast<float>(sample_rate > 0 ? sample_rate : 48000);
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f / sr);    // crossover @ ~200 Hz
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / sr);   // crossover @ ~2 kHz
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = L[i] * g, r = R[i] * g;
            out[2 * i] += l; out[2 * i + 1] += r;
            sum_sq += static_cast<double>(l) * l;
            t.flt_lo += (l - t.flt_lo) * a_lo;
            t.flt_hi += (l - t.flt_hi) * a_hi;
            const float lo = t.flt_lo, mi = t.flt_hi - t.flt_lo, hi = l - t.flt_hi;
            slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
        }
        const float inv = 1.f / (frames > 0 ? frames : 1);
        t.band_low.store(static_cast<float>(std::sqrt(slo * inv)), std::memory_order_relaxed);
        t.band_mid.store(static_cast<float>(std::sqrt(smi * inv)), std::memory_order_relaxed);
        t.band_high.store(static_cast<float>(std::sqrt(shi * inv)), std::memory_order_relaxed);
        const float rms = static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1)));
        t.level.store(rms, std::memory_order_relaxed);
        const float tr = std::max(0.f, (rms - t.tr_baseline) * 6.f);  // onset over baseline
        t.tr_baseline += (rms - t.tr_baseline) * 0.04f;
        t.transient.store(std::min(1.f, tr), std::memory_order_relaxed);
    }
    return any;
}

static void destroy_handle(Vst3Handle* h) {
    if (!h) return;
    if (h->processing) h->processor->setProcessing(false);
    h->destroy(); delete h;
}
void session_destroy(Session* s) {
    if (!s) return;
    auto teardown = [](Track* t) {
        for (Vst3Handle* fx : t->effects_edit) destroy_handle(fx);  // authoritative FX list
        for (Vst3Handle* fx : t->fx_retired)   destroy_handle(fx);  // removed-but-not-freed
        destroy_handle(t->handle);
    };
    for (auto& tp : s->tracks)         teardown(tp.get());
    for (auto& tp : s->tracks_retired) teardown(tp.get());   // tracks removed during the run
    delete s;
}

// Effect queries read the UI-owned list (the audio thread mirrors it).
int session_effect_count(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? static_cast<int>(s->tracks[t]->effects_edit.size()) : 0;
}
const char* session_effect_name(Session* s, int t, int e) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return "";
    auto& fx = s->tracks[t]->effects_edit;
    return (e >= 0 && e < static_cast<int>(fx.size()) && fx[e]) ? fx[e]->plugin_name.c_str() : "";
}
void* session_effect_controller(Session* s, int t, int e) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    auto& fx = s->tracks[t]->effects_edit;
    return (e >= 0 && e < static_cast<int>(fx.size()) && fx[e]) ? fx[e]->controller : nullptr;
}
bool session_add_effect(Session* s, int t, const char* bundle) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size()) || !bundle) return false;
    Vst3Handle* fx = load_effect(bundle, s->sample_rate, &s->host);  // load outside the lock (slow)
    if (!fx) return false;
    Track& tr = *s->tracks[t];
    { std::lock_guard<std::mutex> lk(tr.fx_mtx); tr.effects_edit.push_back(fx); }
    tr.fx_gen.fetch_add(1, std::memory_order_release);
    std::fprintf(stderr, "[Session] track %d + effect: %s\n", t, fx->plugin_name.c_str());
    return true;
}
void session_remove_effect(Session* s, int t, int e) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return;
    Track& tr = *s->tracks[t];
    {
        std::lock_guard<std::mutex> lk(tr.fx_mtx);
        if (e >= 0 && e < static_cast<int>(tr.effects_edit.size())) {
            tr.fx_retired.push_back(tr.effects_edit[e]);   // freed at shutdown, not here
            tr.effects_edit.erase(tr.effects_edit.begin() + e);
        }
    }
    tr.fx_gen.fetch_add(1, std::memory_order_release);
}

// A small catalog of effects offered in the device-chain "+ FX" menu.
static const struct { const char* label; const char* match; } kEffectCatalog[] = {
    { "Yak Delay", "yak" }, { "CHOWTape", "chowtape" }, { "Portal", "portal" },
    { "Infiltrator", "infiltrator" }, { "Airwindows", "airwindows" },
};
int session_available_effect_count() { return static_cast<int>(sizeof(kEffectCatalog) / sizeof(kEffectCatalog[0])); }
const char* session_available_effect_name(int i) {
    return (i >= 0 && i < session_available_effect_count()) ? kEffectCatalog[i].label : "";
}
bool session_add_effect_by_index(Session* s, int t, int i) {
    if (!s || i < 0 || i >= session_available_effect_count()) return false;
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);
    for (const auto& b : bundles)
        if (name_has(b, kEffectCatalog[i].match)) return session_add_effect(s, t, b.c_str());
    return false;
}

// --- Dynamic tracks (create/delete) ---

// A small catalog of instruments offered in the "+ Track" menu.
static const struct { const char* label; const char* match; } kInstrumentCatalog[] = {
    { "Pigments", "pigments" }, { "Serum 2", "serum" }, { "Vital", "vital" },
    { "EZdrummer 3", "ezdrummer" }, { "Battery", "battery" },
};
int session_available_instrument_count() {
    return static_cast<int>(sizeof(kInstrumentCatalog) / sizeof(kInstrumentCatalog[0]));
}
const char* session_available_instrument_name(int i) {
    return (i >= 0 && i < session_available_instrument_count()) ? kInstrumentCatalog[i].label : "";
}

// Resolve `spec` (a catalog label, a plugin-name substring, or a .vst3 path) to a loaded
// instrument with a MIDI input. Returns nullptr if nothing matched/loaded.
static Vst3Handle* load_instrument_spec(Session* s, const char* spec, std::string& out_name) {
    const std::string sp = spec ? spec : "";
    if (sp.size() > 5 && sp.compare(sp.size() - 5, 5, ".vst3") == 0 && std::filesystem::exists(sp)) {
        Vst3Handle* h = vst3_load_plugin(sp.c_str(), "", s->sample_rate, std::string(), &s->host);
        if (h && h->component && h->component->getBusCount(kEvent, kInput) > 0) {
            if (h->processor->setProcessing(true) != kResultOk) {}
            h->processing = true;
            out_name = h->plugin_name.empty() ? sp : h->plugin_name;
            return h;
        }
        if (h) { h->destroy(); delete h; }
        return nullptr;
    }
    const char* match = spec;   // catalog label -> its match substring; else spec is the substring
    for (int i = 0; i < session_available_instrument_count(); ++i)
        if (sp == kInstrumentCatalog[i].label) { match = kInstrumentCatalog[i].match; break; }
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);
    const char* prefer[2] = { match, nullptr };
    return load_role(bundles, prefer, s->sample_rate, &s->host, out_name);
}

int session_add_instrument_track(Session* s, const char* instrument) {
    if (!s || !instrument || !*instrument) return -1;
    if (static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;
    std::string name;
    Vst3Handle* h = load_instrument_spec(s, instrument, name);
    if (!h) { std::fprintf(stderr, "[Session] add track: no instrument matched '%s'\n", instrument); return -1; }
    s->tracks.emplace_back(make_instrument_track(h, name, s->scenes));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_view(s);
    const int idx = static_cast<int>(s->tracks.size()) - 1;
    std::fprintf(stderr, "[Session] + track %d: %s\n", idx, name.c_str());
    return idx;
}

int session_add_audio_track(Session* s) {
    if (!s || static_cast<int>(s->tracks.size()) >= kMaxTracks) return -1;
    auto at = std::make_unique<Track>();
    at->is_audio = true;
    at->name = "Audio";
    at->gain.store(0.7f, std::memory_order_relaxed);
    for (int i = 0; i < 8; ++i) { at->aud_trim0[i].store(0.f); at->aud_trim1[i].store(1.f); }
    at->aud_clips.push_back(gen_sub_pulse(s->sample_rate, 124.0));
    at->aud_clips.push_back(gen_noise_sweep(s->sample_rate, 124.0));
    at->aud_clips.push_back(gen_bell_loop(s->sample_rate, 124.0));
    pad_aud_clips(at.get(), s->scenes);
    at->active.store(-1, std::memory_order_relaxed);
    s->tracks.emplace_back(std::move(at));
    s->tracks.back()->id = s->next_track_id++;
    rebuild_track_view(s);
    const int idx = static_cast<int>(s->tracks.size()) - 1;
    std::fprintf(stderr, "[Session] + audio track %d\n", idx);
    return idx;
}

bool session_remove_track(Session* s, int t) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return false;
    // Move (don't free) the track to the retired list: an in-flight audio block may still
    // hold it in tracks_view until the next sync, so it must outlive this call. Freed at
    // session_destroy (no plugin teardown on the audio thread).
    s->tracks_retired.push_back(std::move(s->tracks[t]));
    s->tracks.erase(s->tracks.begin() + t);
    rebuild_track_view(s);
    std::fprintf(stderr, "[Session] - track %d (retired)\n", t);
    return true;
}

// --- Device parameters (P24). device: 0 = instrument, 1+ = effects. ---
static Vst3Handle* device_handle(Session* s, int t, int dev) {
    if (!s || t < 0 || t >= static_cast<int>(s->tracks.size())) return nullptr;
    Track& tr = *s->tracks[t];
    if (dev == 0) return tr.handle;
    const int e = dev - 1;
    return (e >= 0 && e < static_cast<int>(tr.effects_edit.size())) ? tr.effects_edit[e] : nullptr;
}
int session_param_count(Session* s, int t, int dev) {
    Vst3Handle* h = device_handle(s, t, dev);
    return h ? static_cast<int>(h->params.size()) : 0;
}
const char* session_param_name(Session* s, int t, int dev, int i) {
    Vst3Handle* h = device_handle(s, t, dev);
    return (h && i >= 0 && i < static_cast<int>(h->params.size())) ? h->params[i].name.c_str() : "";
}
uint32_t session_param_id(Session* s, int t, int dev, int i) {
    Vst3Handle* h = device_handle(s, t, dev);
    return (h && i >= 0 && i < static_cast<int>(h->params.size())) ? static_cast<uint32_t>(h->params[i].id) : 0u;
}
float session_param_value(Session* s, int t, int dev, int i) {
    Vst3Handle* h = device_handle(s, t, dev);
    if (!h || !h->controller || i < 0 || i >= static_cast<int>(h->params.size())) return 0.f;
    return static_cast<float>(h->controller->getParamNormalized(h->params[i].id));
}
void session_set_param(Session* s, int t, int dev, uint32_t id, float value) {
    Vst3Handle* h = device_handle(s, t, dev);
    if (!h) return;
    value = value < 0.f ? 0.f : (value > 1.f ? 1.f : value);
    h->param_q.push(id, value);                                        // -> audio thread (process)
    if (h->controller) h->controller->setParamNormalized(id, value);   // -> plugin GUI reflection
}

}  // namespace vivid::session
