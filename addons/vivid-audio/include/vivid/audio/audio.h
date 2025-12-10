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
