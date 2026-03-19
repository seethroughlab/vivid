#include "control/smooth/smooth.h"

// Stubs for Smooth's out-of-line virtuals.
// Real implementations in smooth.cpp depend on WebGPU/thumbnail infrastructure.
// This test only exercises process(), which is inline in the header.

Smooth::~Smooth() = default;

void Smooth::draw_thumbnail(const VividThumbnailContext*) {}
