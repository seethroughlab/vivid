#include "envelope.h"

// Composable support for ChildOp<Envelope>.
//
// Envelope's full plugin implementation in envelope.cpp provides the
// thumbnail pipeline and plugin registration surface. Embedded/composed
// usage only needs concrete out-of-line virtual definitions so consuming
// plugins link cleanly.

Envelope::~Envelope() = default;

void Envelope::draw_thumbnail(const VividThumbnailContext*) {}
