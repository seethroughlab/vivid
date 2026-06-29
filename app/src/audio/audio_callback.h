#pragma once
#include "miniaudio.h"

// The real-time audio callback (a miniaudio ma_device_data_proc): renders the
// session (or a test tone when there's no plugin), advances the master transport,
// and publishes the block RMS level / 3-band energy / transient. device->pUserData
// must point at an AudioState.
void audio_callback(ma_device* device, void* out, const void* in, ma_uint32 frames);
