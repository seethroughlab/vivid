#include "arpeggiator_core.h"

// Shared implementation only.
//
// Arpeggiator ships as two public operator targets:
// - arpeggiator_fr.cpp exports ArpeggiatorFr (frame cadence)
// - arpeggiator_au.cpp exports ArpeggiatorAu (audio cadence)
//
// Keep the shared state and behavior in arpeggiator_core.h so both wrappers
// stay in sync without this file owning registration or cadence.
