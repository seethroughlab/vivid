// Operator Registrations for Core Effects
// Registration is now done via REGISTER_OPERATOR macro.
// See header files for Doxygen documentation.

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
#include <vivid/effects/threshold.h>
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
#include <vivid/effects/copy.h>
#include <vivid/effects/particles.h>
#include <vivid/effects/point_sprites.h>
#include <vivid/effects/gpu_particles.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/canvas.h>
#include <vivid/effects/math_op.h>
#include <vivid/effects/logic_op.h>

using namespace vivid::effects;

// =============================================================================
// Generators (no input required)
// =============================================================================
REGISTER_OPERATOR(Noise, "Generators", "Fractal noise generator", false);
REGISTER_OPERATOR(SolidColor, "Generators", "Solid color fill", false);
REGISTER_OPERATOR(Gradient, "Generators", "Gradient generator", false);
REGISTER_OPERATOR(Ramp, "Generators", "Linear or radial ramp", false);
REGISTER_OPERATOR(Shape, "Generators", "Geometric shape generator", false);
REGISTER_OPERATOR(LFO, "Generators", "Low-frequency oscillator", false);
REGISTER_OPERATOR(Image, "Generators", "Load image from file", false);

// =============================================================================
// Effects (require input)
// =============================================================================
REGISTER_OPERATOR(Blur, "Effects", "Gaussian blur", true);
REGISTER_OPERATOR(HSV, "Effects", "HSV color adjustment", true);
REGISTER_OPERATOR(Brightness, "Effects", "Brightness and contrast adjustment", true);
REGISTER_OPERATOR(Threshold, "Effects", "Binary thresholding", true);
REGISTER_OPERATOR(Transform, "Effects", "2D transform (scale, rotate, translate)", true);
REGISTER_OPERATOR(Mirror, "Effects", "Mirror/flip texture", true);
REGISTER_OPERATOR(Displace, "Effects", "Texture displacement", true);
REGISTER_OPERATOR(Edge, "Effects", "Edge detection", true);
REGISTER_OPERATOR(Pixelate, "Effects", "Pixelation effect", true);
REGISTER_OPERATOR(Tile, "Effects", "Tile/repeat texture", true);
REGISTER_OPERATOR(ChromaticAberration, "Effects", "RGB channel separation", true);
REGISTER_OPERATOR(Bloom, "Effects", "Bloom/glow effect", true);
REGISTER_OPERATOR(Vignette, "Effects", "Vignette effect", true);
REGISTER_OPERATOR(BarrelDistortion, "Effects", "Barrel/pincushion distortion", true);
REGISTER_OPERATOR(Feedback, "Effects", "Feedback loop effect", true);
REGISTER_OPERATOR(FrameCache, "Effects", "Frame history buffer", true);
REGISTER_OPERATOR(TimeMachine, "Effects", "Temporal displacement", true);
REGISTER_OPERATOR(Plexus, "Effects", "Particle connection lines", true);
REGISTER_OPERATOR(Copy, "Effects", "Replicate texture with transforms", true);

// =============================================================================
// Retro Effects
// =============================================================================
REGISTER_OPERATOR(Dither, "Effects", "Dithering effect", true);
REGISTER_OPERATOR(Quantize, "Effects", "Color quantization", true);
REGISTER_OPERATOR(Scanlines, "Effects", "CRT scanlines", true);
REGISTER_OPERATOR(CRTEffect, "Effects", "CRT monitor simulation", true);
REGISTER_OPERATOR(Downsample, "Effects", "Resolution reduction", true);
REGISTER_OPERATOR(FilmGrain, "Effects", "Film grain overlay", true);
REGISTER_OPERATOR(Flash, "Effects", "Flash/strobe effect", true);

// =============================================================================
// Compositing
// =============================================================================
REGISTER_OPERATOR(Composite, "Compositing", "Blend two textures", true);
REGISTER_OPERATOR(Switch, "Compositing", "Switch between inputs", true);

// =============================================================================
// Particles
// =============================================================================
REGISTER_OPERATOR(Particles, "Particles", "CPU particle system", false);
REGISTER_OPERATOR(PointSprites, "Particles", "Point sprite renderer", false);
REGISTER_OPERATOR(GPUParticles, "Particles", "GPU-accelerated particles", false);
REGISTER_OPERATOR(ParticleSystem, "Particles", "Configurable particle system", false);

// =============================================================================
// Canvas (imperative drawing)
// =============================================================================
REGISTER_OPERATOR(Canvas, "Drawing", "Imperative 2D drawing", false);

// =============================================================================
// Math/Logic (value-based operators)
// =============================================================================
REGISTER_OPERATOR_EX(Math, "Utilities", "Mathematical operations", false, vivid::OutputKind::Value);
REGISTER_OPERATOR_EX(Logic, "Utilities", "Logic operations", false, vivid::OutputKind::Value);
