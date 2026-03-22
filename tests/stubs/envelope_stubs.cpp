#include "control/envelope/envelope.h"

// Stubs for Envelope's out-of-line virtuals.
// Real implementations in envelope.cpp depend on WebGPU/thumbnail infrastructure.
// This test only exercises process(), which is inline in the header.

Envelope::~Envelope() = default;

void Envelope::draw_thumbnail(const VividThumbnailContext*) {}
