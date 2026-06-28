// VST3 player — scans/loads an instrument and renders an arpeggio from the
// audio thread, using classic's extracted host (vst3_host_common.h). Kept in
// one TU because the host header is an anonymous-namespace, header-only unit.
#include "vst3_host_common.h"
#include "vst3_host.h"
#include "midi/midi_clip.h"

#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <dirent.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vivid_poc {

struct Vst3Player {
    Vst3HostApp        host;
    Vst3Handle*        handle = nullptr;
    std::string        name;
    std::vector<float> buf_l, buf_r;     // planar scratch the plugin writes into
    uint64_t           steady = 0;       // running sample position
    MidiClip           clip;             // the pattern playing on this instrument
    ClipScheduler      sched;            // transport-locked, sample-accurate
    std::vector<NoteEvent> nev;          // reusable per-block event buffer
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

Vst3Player* vst3_player_create(uint32_t sample_rate) {
    std::vector<std::string> bundles;
    list_vst3("/Library/Audio/Plug-Ins/VST3", bundles);
    if (const char* home = std::getenv("HOME"))
        list_vst3(std::string(home) + "/Library/Audio/Plug-Ins/VST3", bundles);

    auto* p = new Vst3Player();
    for (const auto& path : bundles) {
        Vst3Handle* h = vst3_load_plugin(path.c_str(), "", sample_rate, std::string(), &p->host);
        if (!h) continue;
        // An instrument exposes an event (note) input bus; effects don't.
        if (h->component && h->component->getBusCount(kEvent, kInput) > 0) {
            if (h->processor->setProcessing(true) != kResultOk)
                std::fprintf(stderr, "[Vst3Player] setProcessing(true) failed\n");
            h->processing = true;
            p->handle = h;
            p->name = h->plugin_name.empty() ? path : h->plugin_name;
            // A small looping riff (4 beats) — the MIDI clip this instrument plays.
            p->clip.length = 4.0;
            p->clip.notes = {
                { 62, 0.0, 0.5, 0.85f }, { 65, 0.5, 0.5, 0.70f },
                { 69, 1.0, 0.5, 0.85f }, { 65, 1.5, 0.5, 0.70f },
                { 74, 2.0, 1.0, 0.90f }, { 69, 3.0, 0.9, 0.75f },
            };
            p->sched.reset(&p->clip);
            p->nev.reserve(64);
            std::fprintf(stderr, "[Vst3Player] loaded instrument: %s\n", p->name.c_str());
            break;
        }
        h->destroy();
        delete h;  // not an instrument — try the next bundle
    }
    if (!p->handle) { delete p; return nullptr; }
    return p;
}

const char* vst3_player_name(Vst3Player* p) {
    return (p && p->handle) ? p->name.c_str() : "";
}

bool vst3_player_process(Vst3Player* p, float* out, uint32_t frames,
                         uint32_t sample_rate, double bpm, double beats,
                         uint32_t beats_per_bar) {
    if (!p || !p->handle || !p->handle->processing) return false;

    if (p->buf_l.size() < frames) { p->buf_l.resize(frames); p->buf_r.resize(frames); }
    float* L = p->buf_l.data();
    float* R = p->buf_r.data();
    std::memset(L, 0, frames * sizeof(float));
    std::memset(R, 0, frames * sizeof(float));

    // Schedule the clip's notes for this block, transport-locked + sample-accurate.
    Vst3EventList events;
    const double delta = frames * (bpm / 60.0) / (sample_rate > 0 ? sample_rate : 48000);
    p->nev.clear();
    p->sched.emit(beats, delta, frames, p->nev);
    for (const NoteEvent& ne : p->nev) {
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
            e.noteOff.tuning = 0.f;
        }
        events.addEvent(e);
    }

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

    Vst3ParamChanges param_changes;  // empty; some plugins require a non-null ptr
    param_changes.clear();

    ProcessContext pctx = vst3_build_process_context(&ctx, p->steady);
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

    p->handle->processor->process(data);
    p->steady += frames;

    for (uint32_t i = 0; i < frames; ++i) {
        out[i * 2 + 0] = L[i];
        out[i * 2 + 1] = R[i];
    }
    return true;
}

void vst3_player_destroy(Vst3Player* p) {
    if (!p) return;
    if (p->handle) {
        if (p->handle->processing) p->handle->processor->setProcessing(false);
        p->handle->destroy();
        delete p->handle;
    }
    delete p;
}

}  // namespace vivid_poc
