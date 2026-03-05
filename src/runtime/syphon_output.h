#pragma once

#include <cstdint>
#include <string>
#include <webgpu/webgpu.h>

namespace vivid {

class SyphonOutput {
public:
    SyphonOutput();
    ~SyphonOutput();

    SyphonOutput(const SyphonOutput&) = delete;
    SyphonOutput& operator=(const SyphonOutput&) = delete;

    bool publish(bool enabled,
                 const std::string& server_name,
                 WGPUDevice device,
                 WGPUQueue queue,
                 WGPUTexture texture,
                 uint32_t width,
                 uint32_t height);

    void shutdown();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace vivid
