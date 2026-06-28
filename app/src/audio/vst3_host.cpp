// Multi-track session — N tracks, each a hosted VST3 instrument + per-scene MIDI
// clips, mixed (per-track gain) to the master output with bar-quantized launch.
// Built on classic's extracted host (vst3_host_common.h, anonymous namespace).
#include "vst3_host_common.h"
#include "vst3_host.h"
#include "midi/midi_clip.h"

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
#include <dirent.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vivid_poc {

struct Track {
    Vst3Handle*           handle = nullptr;
    std::string           name;
    std::vector<MidiClip> clips;          // one per scene
    ClipScheduler         sched;
    std::atomic<int>      active{0};
    std::atomic<int>      queued{-1};
    std::atomic<float>    gain{0.8f};
    std::atomic<float>    level{0.f};
    std::atomic<float>    transient{0.f};
    float                 tr_baseline = 0.f;  // onset detector baseline (audio thread)
    std::vector<float>    bl, br;          // planar scratch
    std::vector<NoteEvent> nev;
    uint64_t              steady = 0;
    // Live MIDI editing: the UI edits edit_clips; the audio thread copies them
    // into `clips` element-wise (clip addresses stay stable) when edit_gen bumps.
    std::mutex            edit_mtx;
    std::vector<MidiClip> edit_clips;
    std::atomic<uint64_t> edit_gen{0};
    uint64_t              edit_gen_seen = 0;
};

struct Session {
    Vst3HostApp host;
    std::vector<std::unique_ptr<Track>> tracks;
    int       scenes = 3;
    long long last_bar = -1;
};

static bool name_has(const std::string& path, const char* lower_needle) {
    std::string p = path;
    for (auto& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p.find(lower_needle) != std::string::npos;
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
    t->edit_clips = t->clips;  // editor's mirror starts equal to the live clips
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
    for (const auto& role : kRoles) {
        std::string name;
        Vst3Handle* h = load_role(bundles, role.prefer, sample_rate, &s->host, name);
        if (!h) { std::fprintf(stderr, "[Session] role kind %d unfilled\n", role.kind); continue; }
        s->tracks.emplace_back(make_track(h, name, role.kind));
        std::fprintf(stderr, "[Session] track %zu: %s\n", s->tracks.size() - 1, name.c_str());
    }

    if (s->tracks.empty()) { delete s; return nullptr; }
    std::fprintf(stderr, "[Session] %zu tracks, %d scenes\n", s->tracks.size(), s->scenes);
    return s;
}

int  session_track_count(Session* s) { return s ? static_cast<int>(s->tracks.size()) : 0; }
int  session_scene_count(Session* s) { return s ? s->scenes : 0; }
const char* session_track_name(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size())) ? s->tracks[t]->name.c_str() : "";
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
void* session_track_controller(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->handle) ? s->tracks[t]->handle->controller : nullptr;
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

static void emit_vst3(Vst3EventList& events, const std::vector<NoteEvent>& nev) {
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
            e.noteOn.tuning = 0.f;
        } else {
            e.type = Event::kNoteOffEvent;
            e.noteOff.pitch = static_cast<int16>(ne.pitch);
            e.noteOff.velocity = 0.f;
            e.noteOff.noteId = ne.note_id;
            e.noteOff.channel = 0;
        }
        events.addEvent(e);
    }
}

bool session_process(Session* s, float* out, uint32_t frames, uint32_t sample_rate,
                     double bpm, double beats, uint32_t beats_per_bar) {
    if (!s || s->tracks.empty()) return false;
    std::memset(out, 0, sizeof(float) * 2 * frames);
    const uint32_t bpb = beats_per_bar ? beats_per_bar : 4;
    const long long bar = static_cast<long long>(std::floor(beats / bpb));
    const bool new_bar = bar != s->last_bar;
    s->last_bar = bar;
    const double delta = frames * (bpm / 60.0) / (sample_rate > 0 ? sample_rate : 48000);

    bool any = false;
    for (auto& tp : s->tracks) {
        Track& t = *tp;
        if (!t.handle || !t.handle->processing) continue;
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
                t.edit_gen_seen = t.edit_gen.load(std::memory_order_acquire);
                t.edit_mtx.unlock();
            }
        }
        if (t.bl.size() < frames) { t.bl.resize(frames); t.br.resize(frames); }
        float* L = t.bl.data(); float* R = t.br.data();
        std::memset(L, 0, frames * sizeof(float));
        std::memset(R, 0, frames * sizeof(float));

        Vst3EventList events;
        if (new_bar) {
            const int q = t.queued.load(std::memory_order_relaxed);
            if (q >= 0 && q != t.active.load(std::memory_order_relaxed) && q < static_cast<int>(t.clips.size())) {
                t.nev.clear(); t.sched.flush(t.nev); emit_vst3(events, t.nev);
                t.sched.reset(&t.clips[q]);
                t.active.store(q, std::memory_order_relaxed);
            }
            if (q >= 0) t.queued.store(-1, std::memory_order_relaxed);
        }
        t.nev.clear(); t.sched.emit(beats, delta, frames, t.nev); emit_vst3(events, t.nev);

        VividAudioContext ctx{};
        ctx.sample_rate = sample_rate;
        ctx.metronome_bpm = static_cast<float>(bpm);
        ctx.metronome_beats_per_bar = bpb;
        ctx.metronome_beats_elapsed = beats;
        float* ch[2] = { L, R };
        AudioBusBuffers ob{}; ob.channelBuffers32 = ch; ob.numChannels = 2; ob.silenceFlags = 0;
        Vst3ParamChanges pc; pc.clear();
        ProcessContext pctx = vst3_build_process_context(&ctx, t.steady);
        ProcessData data{};
        data.processMode = kRealtime; data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(frames); data.numInputs = 0; data.numOutputs = 1;
        data.inputs = nullptr; data.outputs = &ob;
        data.inputEvents = &events; data.inputParameterChanges = &pc; data.processContext = &pctx;
        t.handle->processor->process(data);
        t.steady += frames;

        const float g = t.gain.load(std::memory_order_relaxed);
        double sum_sq = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = L[i] * g, r = R[i] * g;
            out[2 * i] += l; out[2 * i + 1] += r;
            sum_sq += static_cast<double>(l) * l;
        }
        const float rms = static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1)));
        t.level.store(rms, std::memory_order_relaxed);
        const float tr = std::max(0.f, (rms - t.tr_baseline) * 6.f);  // onset over baseline
        t.tr_baseline += (rms - t.tr_baseline) * 0.04f;
        t.transient.store(std::min(1.f, tr), std::memory_order_relaxed);
    }
    return any;
}

void session_destroy(Session* s) {
    if (!s) return;
    for (auto& tp : s->tracks) {
        if (tp->handle) {
            if (tp->handle->processing) tp->handle->processor->setProcessing(false);
            tp->handle->destroy();
            delete tp->handle;
        }
    }
    delete s;
}

}  // namespace vivid_poc
