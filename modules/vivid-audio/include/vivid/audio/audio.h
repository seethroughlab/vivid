#pragma once

// Vivid Audio - Main Include
// Include this header to use all audio operators and effects

// Audio sources
#include <vivid/audio/audio_in.h>
#include <vivid/audio/audio_file.h>

// Base effect class
#include <vivid/audio/audio_effect.h>

// Time-based effects
#include <vivid/audio/delay.h>
#include <vivid/audio/echo.h>
#include <vivid/audio/reverb.h>

// Dynamics processing
#include <vivid/audio/compressor.h>
#include <vivid/audio/limiter.h>
#include <vivid/audio/gate.h>

// Modulation effects
#include <vivid/audio/chorus.h>
#include <vivid/audio/flanger.h>
#include <vivid/audio/phaser.h>

// Distortion effects
#include <vivid/audio/overdrive.h>
#include <vivid/audio/bitcrush.h>

// Lo-fi/Vintage effects
#include <vivid/audio/tape_effect.h>

// Audio analysis
#include <vivid/audio/audio_analyzer.h>
#include <vivid/audio/levels.h>
#include <vivid/audio/fft.h>
#include <vivid/audio/band_split.h>
#include <vivid/audio/beat_detect.h>

// Synthesis
#include <vivid/audio/oscillator.h>
#include <vivid/audio/envelope.h>
#include <vivid/audio/synth.h>
#include <vivid/audio/poly_synth.h>
#include <vivid/audio/wavetable_synth.h>
#include <vivid/audio/fm_synth.h>
#include <vivid/audio/granular.h>
#include <vivid/audio/noise_gen.h>
#include <vivid/audio/crackle.h>
#include <vivid/audio/pitch_env.h>
#include <vivid/audio/formant.h>

// Envelope variants
#include <vivid/audio/decay.h>
#include <vivid/audio/ar.h>

// Drum synthesis
#include <vivid/audio/kick.h>
#include <vivid/audio/snare.h>
#include <vivid/audio/hihat.h>
#include <vivid/audio/clap.h>
#include <vivid/audio/tom.h>
#include <vivid/audio/cymbal.h>
#include <vivid/audio/fm_drum.h>
#include <vivid/audio/clang.h>
#include <vivid/audio/drum_stack.h>
#include <vivid/audio/drum_kit.h>

// Sequencing
#include <vivid/audio/clock.h>
#include <vivid/audio/sequencer.h>
#include <vivid/audio/arpeggiator.h>
#include <vivid/audio/euclidean.h>
#include <vivid/audio/song.h>

// Filters
#include <vivid/audio/audio_filter.h>
#include <vivid/audio/ladder_filter.h>
#include <vivid/audio/comb_filter.h>

// Utilities
#include <vivid/audio/audio_mixer.h>
#include <vivid/audio/audio_gain.h>

// Sampling
#include <vivid/audio/sample_bank.h>
#include <vivid/audio/sample_player.h>
#include <vivid/audio/sampler.h>
#include <vivid/audio/multi_sampler.h>

// Glitch effects
#include <vivid/audio/glitch/circular_buffer.h>
#include <vivid/audio/glitch/rate_utils.h>
#include <vivid/audio/glitch/beat_repeat.h>
#include <vivid/audio/glitch/reverse.h>
#include <vivid/audio/glitch/stutter.h>
#include <vivid/audio/glitch/scratch.h>
#include <vivid/audio/glitch/tape_stop.h>
#include <vivid/audio/glitch/frequency_shift.h>
#include <vivid/audio/glitch/stretch.h>
#include <vivid/audio/glitch/glitch.h>

// Musical constants
#include <vivid/audio/notes.h>

// Audio output (from vivid-core)
#include <vivid/audio_output.h>
