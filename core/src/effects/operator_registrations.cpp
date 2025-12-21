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
#include <vivid/effects/canvas.h>
#include <vivid/effects/math_op.h>
#include <vivid/effects/logic_op.h>

using namespace vivid::effects;

// Generators (no input required)
REGISTER_OPERATOR(Noise, "Generators", "Fractal noise generator", false);
REGISTER_OPERATOR(SolidColor, "Generators", "Solid color fill", false);
REGISTER_OPERATOR(Gradient, "Generators", "Color gradient", false);
REGISTER_OPERATOR(Ramp, "Generators", "Animated HSV gradient", false);
REGISTER_OPERATOR(Shape, "Generators", "SDF shape generator", false);
REGISTER_OPERATOR_EX(LFO, "Generators", "Low frequency oscillator", false, vivid::OutputKind::Value);
REGISTER_OPERATOR(Image, "Generators", "Load image from file", false);

// Effects (require input)
REGISTER_OPERATOR(Blur, "Effects", "Gaussian blur", true);
REGISTER_OPERATOR(HSV, "Effects", "Hue/saturation/value adjustment", true);
REGISTER_OPERATOR(Brightness, "Effects", "Brightness and contrast", true);
REGISTER_OPERATOR(Transform, "Effects", "Scale, rotate, translate", true);
REGISTER_OPERATOR(Mirror, "Effects", "Axis mirroring and kaleidoscope", true);
REGISTER_OPERATOR(Displace, "Effects", "Texture displacement", true);
REGISTER_OPERATOR(Edge, "Effects", "Edge detection", true);
REGISTER_OPERATOR(Pixelate, "Effects", "Mosaic/pixelation effect", true);
REGISTER_OPERATOR(Tile, "Effects", "Texture tiling", true);
REGISTER_OPERATOR(ChromaticAberration, "Effects", "RGB channel separation", true);
REGISTER_OPERATOR(Bloom, "Effects", "Glow/bloom effect", true);
REGISTER_OPERATOR(Vignette, "Effects", "Edge darkening vignette", true);
REGISTER_OPERATOR(BarrelDistortion, "Effects", "Barrel/pincushion distortion", true);
REGISTER_OPERATOR(Feedback, "Effects", "Frame feedback loop", true);
REGISTER_OPERATOR(FrameCache, "Effects", "Buffer multiple frames", true);
REGISTER_OPERATOR(TimeMachine, "Effects", "Temporal displacement", true);
REGISTER_OPERATOR(Plexus, "Effects", "Connected particle network", true);

// Retro Effects
REGISTER_OPERATOR(Dither, "Retro", "Ordered dithering", true);
REGISTER_OPERATOR(Quantize, "Retro", "Color quantization", true);
REGISTER_OPERATOR(Scanlines, "Retro", "CRT scanline effect", true);
REGISTER_OPERATOR(CRTEffect, "Retro", "Full CRT simulation", true);
REGISTER_OPERATOR(Downsample, "Retro", "Low resolution effect", true);
REGISTER_OPERATOR(FilmGrain, "Retro", "Film grain overlay", true);

// Compositing
REGISTER_OPERATOR(Composite, "Compositing", "Blend two textures", true);
REGISTER_OPERATOR(Switch, "Compositing", "Switch between inputs", true);

// Particles
REGISTER_OPERATOR(Particles, "Particles", "2D particle system", false);
REGISTER_OPERATOR(PointSprites, "Particles", "Point-based particles", false);

// Canvas
REGISTER_OPERATOR(Canvas, "Canvas", "Imperative 2D drawing", false);

// Math/Logic
REGISTER_OPERATOR_EX(Math, "Math/Logic", "Mathematical operations", false, vivid::OutputKind::Value);
REGISTER_OPERATOR_EX(Logic, "Math/Logic", "Logical comparisons", false, vivid::OutputKind::Value);
