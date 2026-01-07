// Operator Registrations for Core Effects
// This file registers all core operators for CLI introspection
// All metadata is now self-contained in each operator's describe() method

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

// =============================================================================
// Generators (no input required)
// =============================================================================
REGISTER(Noise);
REGISTER(SolidColor);
REGISTER(Gradient);
REGISTER(Ramp);
REGISTER(Shape);
REGISTER(LFO);
REGISTER(Image);

// =============================================================================
// Effects (require input)
// =============================================================================
REGISTER(Blur);
REGISTER(HSV);
REGISTER(Brightness);
REGISTER(Transform);
REGISTER(Mirror);
REGISTER(Displace);
REGISTER(Edge);
REGISTER(Pixelate);
REGISTER(Tile);
REGISTER(ChromaticAberration);
REGISTER(Bloom);
REGISTER(Vignette);
REGISTER(BarrelDistortion);
REGISTER(Feedback);
REGISTER(FrameCache);
REGISTER(TimeMachine);
REGISTER(Plexus);

// =============================================================================
// Retro Effects
// =============================================================================
REGISTER(Dither);
REGISTER(Quantize);
REGISTER(Scanlines);
REGISTER(CRTEffect);
REGISTER(Downsample);
REGISTER(FilmGrain);
REGISTER(Flash);

// =============================================================================
// Compositing
// =============================================================================
REGISTER(Composite);
REGISTER(Switch);

// =============================================================================
// Particles
// =============================================================================
REGISTER(Particles);
REGISTER(PointSprites);
REGISTER(GPUParticles);
REGISTER(ParticleSystem);

// =============================================================================
// Canvas (imperative drawing)
// =============================================================================
REGISTER(Canvas);

// =============================================================================
// Math/Logic (value-based operators)
// =============================================================================
REGISTER(Math);
REGISTER(Logic);
