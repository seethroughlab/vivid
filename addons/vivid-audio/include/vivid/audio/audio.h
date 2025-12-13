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

// Sequencing
#include <vivid/audio/clock.h>
#include <vivid/audio/sequencer.h>
#include <vivid/audio/euclidean.h>

// Utilities
#include <vivid/audio/audio_filter.h>
#include <vivid/audio/audio_mixer.h>
#include <vivid/audio/audio_gain.h>

// Sampling
#include <vivid/audio/sample_bank.h>
#include <vivid/audio/sample_player.h>

// Musical constants
#include <vivid/audio/notes.h>
