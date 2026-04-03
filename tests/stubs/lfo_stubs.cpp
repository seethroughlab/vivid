#include "control/lfo/lfo.h"

// Stubs for LFO's out-of-line virtuals.
// Real implementations in lfo.cpp depend on thumbnail infrastructure.
// This test only exercises process(), which is inline in the header.

void LFO::draw_thumbnail(const VividThumbnailContext*) {}
