#include "smooth.h"

// Composable support for ChildOp<Smooth>.
//
// Smooth's full plugin implementation in smooth.cpp provides thumbnail support
// and the plugin registration surface. Embedded/composed usage only needs the
// out-of-line virtual definitions so the concrete type links cleanly.

Smooth::~Smooth() = default;

void Smooth::draw_thumbnail(const VividThumbnailContext*) {}
