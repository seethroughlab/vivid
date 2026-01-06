// Operator Registrations for Core Effects
// This file registers all core operators for CLI introspection

#include <vivid/operator_registry.h>

// Include all operators
#include <vivid/effects/noise.h>
#include <vivid/effects/solid_color.h>
#include <vivid/effects/gradient.h>
#include <vivid/effects/ramp.h>
#include <vivid/effects/shape.h>
#include <vivid/effects/lfo.h>
#include <vivid/effects/image.h>
#include <vivid/effects/blur.h>
#include <vivid/effects/hsv.h>
#include <vivid/effects/brightness.h>
#include <vivid/effects/transform.h>
#include <vivid/effects/mirror.h>
#include <vivid/effects/displace.h>
#include <vivid/effects/edge.h>
#include <vivid/effects/pixelate.h>
#include <vivid/effects/tile.h>
#include <vivid/effects/chromatic_aberration.h>
#include <vivid/effects/bloom.h>
#include <vivid/effects/vignette.h>
#include <vivid/effects/film_grain.h>
#include <vivid/effects/flash.h>
#include <vivid/effects/barrel_distortion.h>
#include <vivid/effects/feedback.h>
#include <vivid/effects/frame_cache.h>
#include <vivid/effects/time_machine.h>
#include <vivid/effects/plexus.h>
#include <vivid/effects/dither.h>
#include <vivid/effects/quantize.h>
#include <vivid/effects/scanlines.h>
#include <vivid/effects/crt_effect.h>
#include <vivid/effects/downsample.h>
#include <vivid/effects/composite.h>
#include <vivid/effects/switch_op.h>
#include <vivid/effects/particles.h>
#include <vivid/effects/point_sprites.h>
#include <vivid/effects/gpu_particles.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/canvas.h>
#include <vivid/effects/math_op.h>
#include <vivid/effects/logic_op.h>

using namespace vivid::effects;

// Generators (no input required)
REGISTER_OPERATOR_FULL(Noise, "Generators", "Fractal noise generator", false)
    .related({"Gradient", "Displace", "FBM"})
    .examples({"modules/vivid-core/examples/hello-noise/"})
    ;
REGISTER_OPERATOR_FULL(SolidColor, "Generators", "Solid color fill", false)
    .related({"Gradient", "Shape"})
    ;
REGISTER_OPERATOR_FULL(Gradient, "Generators", "Color gradient", false)
    .related({"Noise", "Ramp", "SolidColor"})
    ;
REGISTER_OPERATOR_FULL(Ramp, "Generators", "Animated HSV gradient", false)
    .related({"Gradient", "LFO"})
    ;
REGISTER_OPERATOR_FULL(Shape, "Generators", "SDF shape generator", false)
    .related({"Canvas", "SolidColor"})
    ;
REGISTER_OPERATOR_EX(LFO, "Generators", "Low frequency oscillator", false, vivid::OutputKind::Value);
REGISTER_OPERATOR_FULL(Image, "Generators", "Load image from file", false)
    .limitations({"Supports PNG, JPG, BMP, TGA", "Large images may cause memory pressure"})
    .related({"Video", "Transform"})
    ;

// Effects (require input)
REGISTER_OPERATOR_FULL(Blur, "Effects", "Gaussian blur", true)
    .related({"Bloom", "Edge", "Feedback"})
    ;
REGISTER_OPERATOR_FULL(HSV, "Effects", "Hue/saturation/value adjustment", true)
    .related({"Brightness", "Quantize"})
    ;
REGISTER_OPERATOR_FULL(Brightness, "Effects", "Brightness and contrast", true)
    .related({"HSV", "Bloom"})
    ;
REGISTER_OPERATOR_FULL(Transform, "Effects", "Scale, rotate, translate", true)
    .related({"Mirror", "Tile", "Feedback"})
    ;
REGISTER_OPERATOR_FULL(Mirror, "Effects", "Axis mirroring and kaleidoscope", true)
    .related({"Transform", "Tile"})
    ;
REGISTER_OPERATOR_FULL(Displace, "Effects", "Texture displacement", true)
    .related({"Noise", "Transform", "TimeMachine"})
    ;
REGISTER_OPERATOR_FULL(Edge, "Effects", "Edge detection", true)
    .related({"Blur", "Brightness"})
    ;
REGISTER_OPERATOR_FULL(Pixelate, "Effects", "Mosaic/pixelation effect", true)
    .related({"Downsample", "Quantize", "Dither"})
    ;
REGISTER_OPERATOR_FULL(Tile, "Effects", "Texture tiling", true)
    .related({"Transform", "Mirror"})
    ;
REGISTER_OPERATOR_FULL(ChromaticAberration, "Effects", "RGB channel separation", true)
    .related({"BarrelDistortion", "CRTEffect"})
    ;
REGISTER_OPERATOR_FULL(Bloom, "Effects", "Glow/bloom effect", true)
    .related({"Blur", "Brightness", "Flash"})
    ;
REGISTER_OPERATOR_FULL(Vignette, "Effects", "Edge darkening vignette", true)
    .related({"CRTEffect", "BarrelDistortion"})
    ;
REGISTER_OPERATOR_FULL(BarrelDistortion, "Effects", "Barrel/pincushion distortion", true)
    .related({"ChromaticAberration", "CRTEffect", "Vignette"})
    ;
REGISTER_OPERATOR_FULL(Feedback, "Effects", "Frame feedback loop", true)
    .limitations({"Requires careful decay settings to avoid whiteout", "High memory usage at large resolutions"})
    .related({"Particles", "Transform", "Blur"})
    .examples({"modules/vivid-core/examples/feedback/"})
    ;
REGISTER_OPERATOR_FULL(FrameCache, "Effects", "Buffer multiple frames", true)
    .limitations({"Memory usage scales with frame count", "Max 128 frames"})
    .related({"TimeMachine", "Feedback"})
    ;
REGISTER_OPERATOR_FULL(TimeMachine, "Effects", "Temporal displacement", true)
    .related({"FrameCache", "Displace", "Feedback"})
    ;
REGISTER_OPERATOR_FULL(Plexus, "Effects", "Connected particle network", true)
    .limitations({"CPU-based line drawing", "Performance degrades with many points"})
    .related({"Particles", "PointSprites"})
    ;

// Retro Effects
REGISTER_OPERATOR_FULL(Dither, "Retro", "Ordered dithering", true)
    .related({"Quantize", "Downsample", "Pixelate"})
    ;
REGISTER_OPERATOR_FULL(Quantize, "Retro", "Color quantization", true)
    .related({"Dither", "Downsample", "HSV"})
    ;
REGISTER_OPERATOR_FULL(Scanlines, "Retro", "CRT scanline effect", true)
    .related({"CRTEffect", "Downsample"})
    ;
REGISTER_OPERATOR_FULL(CRTEffect, "Retro", "Full CRT simulation", true)
    .related({"Scanlines", "BarrelDistortion", "ChromaticAberration", "Vignette"})
    .examples({"modules/vivid-core/examples/retro-crt/"})
    ;
REGISTER_OPERATOR_FULL(Downsample, "Retro", "Low resolution effect", true)
    .related({"Pixelate", "Quantize", "Dither"})
    ;
REGISTER_OPERATOR_FULL(FilmGrain, "Retro", "Film grain overlay", true)
    .related({"Noise", "Composite"})
    ;
REGISTER_OPERATOR_FULL(Flash, "Retro", "Beat-synced flash overlay", true)
    .limitations({"Requires trigger() call or audio input for sync"})
    .related({"Bloom", "Composite"})
    ;

// Compositing
REGISTER_OPERATOR_FULL(Composite, "Compositing", "Blend two textures", true)
    .related({"Switch", "Transform", "Canvas"})
    ;
REGISTER_OPERATOR_FULL(Switch, "Compositing", "Switch between inputs", true)
    .limitations({"Max 8 inputs"})
    .related({"Composite", "Logic"})
    ;

// Particles
REGISTER_OPERATOR_FULL(Particles, "Particles", "2D particle system", false)
    .limitations({"Turbulence is random, not curl noise/flow fields", "Single attractor only", "CPU-based physics (~10k particle limit)"})
    .related({"Plexus", "PointSprites", "Feedback"})
    .examples({"modules/vivid-core/examples/particles/"})
    ;
REGISTER_OPERATOR_FULL(PointSprites, "Particles", "Point-based particles", false)
    .related({"Particles", "Plexus"})
    ;
REGISTER_OPERATOR_FULL(GPUParticles, "Particles", "GPU compute particle system", false)
    .limitations({"Requires WebGPU compute shader support", "Fixed 64-byte particle struct"})
    .related({"Particles", "Plexus", "PointSprites", "ParticleSystem"})
    ;
REGISTER_OPERATOR_FULL(ParticleSystem, "Particles", "Unified particle system with CPU/GPU simulation and multiple render modes", false)
    .limitations({"GPU simulation and Billboard/Mesh rendering are work in progress"})
    .related({"Particles", "GPUParticles", "Particles3D"})
    .examples({"modules/vivid-core/examples/particle-forces/"})
    ;

// Canvas
REGISTER_OPERATOR_FULL(Canvas, "Canvas", "Imperative 2D drawing", false)
    .related({"Shape", "Composite"})
    .examples({"modules/vivid-core/examples/canvas-drawing/"})
    ;

// Math/Logic (keep basic registration for correct OutputKind::Value)
REGISTER_OPERATOR_EX(Math, "Math/Logic", "Mathematical operations", false, vivid::OutputKind::Value);
REGISTER_OPERATOR_EX(Logic, "Math/Logic", "Logical comparisons", false, vivid::OutputKind::Value);
