#pragma once
#include <cstdint>

// Native audio-operator runtime (AO-1). Instantiates OperatorBase+AudioProcessable
// operators from the shared OpRegistry and runs them on the audio thread. Kept in its
// own translation unit so it can include the full operator_api VividAudioContext without
// colliding with the minimal VST3 tempo stub (audio/vivid_audio_context.h) that
// vst3_host.cpp uses. The interface here is primitives + an opaque handle only.
//
// An audio operator is a SOURCE (no audio-input port — an instrument or generator; it
// writes L/R and may read note events) or an EFFECT (has an audio-input port; it
// transforms L/R). Param changes are set from the UI/main thread (lock-free atomics);
// processing is RT-safe (no alloc/lock, block <= kMaxBlock).
namespace vivid {

class OpRegistry;
namespace session { struct NoteEvent; }

struct AudioOp;   // opaque native audio-operator instance

// --- UI/main thread ---
AudioOp*    audio_op_create(OpRegistry& reg, const char* type_name);  // null if not a valid audio op
// Enumerate registered audio operators for the device pickers. want_source: true =
// instruments/generators (no audio input), false = effects (has audio input).
int         audio_op_registry_count(OpRegistry& reg, bool want_source);
const char* audio_op_registry_name(OpRegistry& reg, bool want_source, int idx);   // stable registry key
void        audio_op_destroy(AudioOp*);
const char* audio_op_type(const AudioOp*);
bool        audio_op_is_source(const AudioOp*);      // true = instrument/generator (no audio input)
int         audio_op_param_count(const AudioOp*);
const char* audio_op_param_name(const AudioOp*, int i);
float       audio_op_param_get(const AudioOp*, int i);
float       audio_op_param_min(const AudioOp*, int i);      // Param<> range (for UI normalization)
float       audio_op_param_max(const AudioOp*, int i);
void        audio_op_param_set(AudioOp*, int i, float v);   // any thread (single UI producer); lock-free

// --- Audio thread (RT-safe) ---
// Source: writes L/R (ignores input), reading `notes` if it's an instrument.
// Effect: transforms L/R in place. `beats_elapsed` is the transport position.
void audio_op_process(AudioOp*, float* L, float* R, uint32_t frames, uint32_t sample_rate,
                      float bpm, uint32_t beats_per_bar, double beats_elapsed,
                      const session::NoteEvent* notes, uint32_t note_count);

}  // namespace vivid
