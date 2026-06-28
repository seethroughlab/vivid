// Session — one hosted VST3 instrument + a list of launchable MIDI clips, with
// transport-quantized (next-bar) clip switching driven from the audio thread.
// Built on classic's extracted host (vst3_host_common.h, anonymous namespace),
// so everything lives in this one TU.
#include "vst3_host_common.h"
#include "vst3_host.h"
#include "midi/midi_clip.h"

#include <vector>
#include <string>
#include <atomic>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <dirent.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vivid_poc {

struct Session {
    Vst3HostApp        host;
    Vst3Handle*        handle = nullptr;
    std::string        name;

    std::vector<MidiClip> clips;
    ClipScheduler         sched;
    std::atomic<int>      active{0};
    std::atomic<int>      queued{-1};
    long long             last_bar = -1;

    std::vector<float>     buf_l, buf_r;   // planar scratch
    std::vector<NoteEvent> nev;            // reusable per-block event buffer
    uint64_t               steady = 0;
};

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

// Three obviously-different riffs so launching is audible.
static std::vector<MidiClip> make_clips() {
    MidiClip a; a.length = 4.0; a.notes = {  // mid Dm riff
        {62,0.0,0.5,0.85f},{65,0.5,0.5,0.70f},{69,1.0,0.5,0.85f},
        {65,1.5,0.5,0.70f},{74,2.0,1.0,0.90f},{69,3.0,0.9,0.75f} };
    MidiClip b; b.length = 4.0; b.notes = {  // high sparse arp
        {81,0.0,0.4,0.8f},{84,1.0,0.4,0.75f},{88,2.0,0.4,0.8f},{86,3.0,0.9,0.7f} };
    MidiClip c; c.length = 4.0; c.notes = {  // low bass pulse (eighths)
        {38,0.0,0.4,0.95f},{38,0.5,0.4,0.7f},{41,1.0,0.4,0.9f},{38,1.5,0.4,0.7f},
        {36,2.0,0.4,0.95f},{36,2.5,0.4,0.7f},{43,3.0,0.4,0.9f},{41,3.5,0.4,0.7f} };
    return { a, b, c };
}

Session* session_create(uint32_t sample_rate) {
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);

    auto* s = new Session();
    for (const auto& path : bundles) {
        Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sample_rate, std::string(), &s->host);
        if (!h) continue;
        if (h->component && h->component->getBusCount(kEvent, kInput) > 0) {  // an instrument
            if (h->processor->setProcessing(true) != kResultOk)
                std::fprintf(stderr, "[Session] setProcessing(true) failed\n");
            h->processing = true;
            s->handle = h;
            s->name = h->plugin_name.empty() ? path : h->plugin_name;
            s->clips = make_clips();
            s->sched.reset(&s->clips[0]);
            s->nev.reserve(64);
            std::fprintf(stderr, "[Session] instrument: %s, %zu clips\n",
                         s->name.c_str(), s->clips.size());
            break;
        }
        h->destroy();
        delete h;
    }
    if (!s->handle) { delete s; return nullptr; }
    return s;
}

const char* session_name(Session* s) { return (s && s->handle) ? s->name.c_str() : ""; }
int session_clip_count(Session* s)   { return s ? static_cast<int>(s->clips.size()) : 0; }
int session_active_clip(Session* s)  { return s ? s->active.load(std::memory_order_relaxed) : -1; }
int session_queued_clip(Session* s)  { return s ? s->queued.load(std::memory_order_relaxed) : -1; }

void session_launch(Session* s, int index) {
    if (s && index >= 0 && index < static_cast<int>(s->clips.size()))
        s->queued.store(index, std::memory_order_relaxed);
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
    if (!s || !s->handle || !s->handle->processing) return false;

    if (s->buf_l.size() < frames) { s->buf_l.resize(frames); s->buf_r.resize(frames); }
    float* L = s->buf_l.data();
    float* R = s->buf_r.data();
    std::memset(L, 0, frames * sizeof(float));
    std::memset(R, 0, frames * sizeof(float));

    Vst3EventList events;

    // Quantized launch: apply any queued clip at the bar boundary.
    long long bar = static_cast<long long>(std::floor(beats / (beats_per_bar > 0 ? beats_per_bar : 4)));
    if (bar != s->last_bar) {
        s->last_bar = bar;
        int q = s->queued.load(std::memory_order_relaxed);
        if (q >= 0) {
            if (q != s->active.load(std::memory_order_relaxed) && q < static_cast<int>(s->clips.size())) {
                s->nev.clear();
                s->sched.flush(s->nev);                 // release the old clip's notes
                emit_vst3(events, s->nev);
                s->sched.reset(&s->clips[q]);
                s->active.store(q, std::memory_order_relaxed);
            }
            s->queued.store(-1, std::memory_order_relaxed);
        }
    }

    // Schedule the active clip for this block (transport-locked, sample-accurate).
    const double delta = frames * (bpm / 60.0) / (sample_rate > 0 ? sample_rate : 48000);
    s->nev.clear();
    s->sched.emit(beats, delta, frames, s->nev);
    emit_vst3(events, s->nev);

    VividAudioContext ctx{};
    ctx.sample_rate = sample_rate;
    ctx.metronome_bpm = static_cast<float>(bpm);
    ctx.metronome_beats_per_bar = beats_per_bar;
    ctx.metronome_beats_elapsed = beats;

    float* channels[2] = { L, R };
    AudioBusBuffers output_bus{};
    output_bus.channelBuffers32 = channels;
    output_bus.numChannels = 2;
    output_bus.silenceFlags = 0;

    Vst3ParamChanges param_changes;
    param_changes.clear();

    ProcessContext pctx = vst3_build_process_context(&ctx, s->steady);
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = static_cast<int32>(frames);
    data.numInputs = 0;
    data.numOutputs = 1;
    data.inputs = nullptr;
    data.outputs = &output_bus;
    data.inputEvents = &events;
    data.outputEvents = nullptr;
    data.inputParameterChanges = &param_changes;
    data.outputParameterChanges = nullptr;
    data.processContext = &pctx;

    s->handle->processor->process(data);
    s->steady += frames;

    for (uint32_t i = 0; i < frames; ++i) {
        out[i * 2 + 0] = L[i];
        out[i * 2 + 1] = R[i];
    }
    return true;
}

void session_destroy(Session* s) {
    if (!s) return;
    if (s->handle) {
        if (s->handle->processing) s->handle->processor->setProcessing(false);
        s->handle->destroy();
        delete s->handle;
    }
    delete s;
}

}  // namespace vivid_poc
