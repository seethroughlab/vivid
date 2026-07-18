#pragma once

#include "cli/control_handlers.h"

namespace vivid {

void register_audio_analysis_handlers(Handlers& handlers);
void register_audio_clip_pool_handlers(Handlers& handlers);
void register_audio_device_handlers(Handlers& handlers);
void register_audio_graph_handlers(Handlers& handlers);
void register_audio_catalog_handlers(Handlers& handlers);

}  // namespace vivid
