#pragma once
#include <cstdint>

// Minimal façade over the extracted VST3 host (vst3_host_common.h lives in an
// anonymous namespace, so the real work happens in vst3_host.cpp and main only
// talks to these C-style entry points).
//
// P2: a Session = one hosted instrument + a list of launchable MIDI clips. Clip
// launches are queued on the main thread and applied on the audio thread at the
// next bar boundary (Ableton-style quantized launch).
namespace vivid_poc {

struct Session;  // opaque

// Scan the standard macOS VST3 dirs, load the first instrument, set up its
// clips, and setProcessing(true). Returns nullptr if no instrument is found.
Session* session_create(uint32_t sample_rate);

const char* session_name(Session*);
int  session_clip_count(Session*);
int  session_active_clip(Session*);   // currently playing (audio thread truth)
int  session_queued_clip(Session*);   // -1 if none pending

// Main thread: request clip `index` — applied at the next bar boundary.
void session_launch(Session*, int index);

// Audio thread: render `frames` of interleaved stereo f32 into `out`, scheduling
// the active clip and applying any queued launch on the bar. Returns false (and
// writes nothing) if there is no plugin.
bool session_process(Session*, float* out, uint32_t frames, uint32_t sample_rate,
                     double bpm, double beats, uint32_t beats_per_bar);

void session_destroy(Session*);

}  // namespace vivid_poc
