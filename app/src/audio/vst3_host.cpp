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
    std::vector<float>    bl, br;          // planar scratch
    std::vector<NoteEvent> nev;
    uint64_t              steady = 0;
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

static Track* make_track(Vst3Handle* h, const std::string& name, int index) {
    static const int kOffsets[3] = { 0, -12, 7 };  // lead / bass / harmony
    auto* t = new Track();
    t->handle = h;
    t->name = name;
    const int o = kOffsets[index % 3];
    for (auto& bp : base_patterns()) t->clips.push_back(transpose(bp, o));
    t->sched.reset(&t->clips[0]);
    t->nev.reserve(64);
    return t;
}

Session* session_create(uint32_t sample_rate) {
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);

    // Prefer Arturia Pigments as the primary instrument (track 0).
    std::stable_sort(bundles.begin(), bundles.end(), [](const std::string& a, const std::string& b) {
        return (name_has(a, "pigments") ? 0 : 1) < (name_has(b, "pigments") ? 0 : 1);
    });

    auto* s = new Session();
    const int kTarget = 3;
    std::string first_path;

    // Load up to kTarget distinct instruments.
    for (const auto& path : bundles) {
        if (static_cast<int>(s->tracks.size()) >= kTarget) break;
        Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sample_rate, std::string(), &s->host);
        if (!h) continue;
        if (!(h->component && h->component->getBusCount(kEvent, kInput) > 0)) { h->destroy(); delete h; continue; }
        if (h->processor->setProcessing(true) != kResultOk) {}
        h->processing = true;
        const int idx = static_cast<int>(s->tracks.size());
        s->tracks.emplace_back(make_track(h, h->plugin_name.empty() ? path : h->plugin_name, idx));
        if (first_path.empty()) first_path = path;
    }
    // Top up with extra instances of the first instrument if we found too few.
    while (static_cast<int>(s->tracks.size()) < kTarget && !first_path.empty()) {
        Vst3Handle* h = vst3_load_plugin(first_path.c_str(), "", sample_rate, std::string(), &s->host);
        if (!h) break;
        if (h->processor->setProcessing(true) != kResultOk) {}
        h->processing = true;
        const int idx = static_cast<int>(s->tracks.size());
        std::string nm = (h->plugin_name.empty() ? first_path : h->plugin_name) + " " + std::to_string(idx + 1);
        s->tracks.emplace_back(make_track(h, nm, idx));
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
void* session_track_controller(Session* s, int t) {
    return (s && t >= 0 && t < static_cast<int>(s->tracks.size()) && s->tracks[t]->handle) ? s->tracks[t]->handle->controller : nullptr;
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
        t.level.store(static_cast<float>(std::sqrt(sum_sq / (frames > 0 ? frames : 1))), std::memory_order_relaxed);
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
