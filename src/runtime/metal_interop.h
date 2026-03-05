#pragma once

#include <webgpu/webgpu.h>

namespace vivid::metal_interop {

bool available();
void* native_metal_device(WGPUDevice device);
void* native_metal_command_queue(WGPUQueue queue);
void* native_metal_texture(WGPUTexture texture);

} // namespace vivid::metal_interop
