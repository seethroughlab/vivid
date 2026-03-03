#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

// =============================================================================
// Instancer3D Operator — renders one mesh N times with per-instance transforms
// =============================================================================

struct Instancer3D : vivid::OperatorBase {
    static constexpr const char* kName   = "Instancer3D";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   count   {"count",   16, 1, 4096};
    vivid::Param<int>   layout  {"layout",  0, {"Grid", "Circle", "Line"}};
    vivid::Param<float> spacing {"spacing", 2.0f, 0.1f, 20.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(count, "Instancer");
        vivid::param_group(layout, "Instancer");
        vivid::param_group(spacing, "Instancer");

        out.push_back(&count);
        out.push_back(&layout);
        out.push_back(&spacing);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"scene",     VIVID_PORT_GPU_SCENE,     VIVID_PORT_INPUT});
        out.push_back({"positions", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"scales",    VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"colors",    VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"scene",     VIVID_PORT_GPU_SCENE,     VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        // Check input scene
        if (!gpu->input_scenes || gpu->input_scene_count == 0 || !gpu->input_scenes[0]) return;
        const auto* input = gpu->input_scenes[0];
        if (!input->vertex_buffer || input->index_count == 0) return;

        // Read spreads (input port indices: scene=0, positions=1, scales=2, colors=3)
        const float* pos_data = nullptr;
        uint32_t pos_len = 0;
        const float* scale_data = nullptr;
        uint32_t scale_len = 0;
        const float* color_data = nullptr;
        uint32_t color_len = 0;

        if (ctx->input_spreads) {
            if (ctx->input_spreads[1].length > 0) {
                pos_data = ctx->input_spreads[1].data;
                pos_len  = ctx->input_spreads[1].length;
            }
            if (ctx->input_spreads[2].length > 0) {
                scale_data = ctx->input_spreads[2].data;
                scale_len  = ctx->input_spreads[2].length;
            }
            if (ctx->input_spreads[3].length > 0) {
                color_data = ctx->input_spreads[3].data;
                color_len  = ctx->input_spreads[3].length;
            }
        }

        // Determine instance count: from positions spread (3 floats per instance) or param
        uint32_t n = static_cast<uint32_t>(count.int_value());
        if (pos_data && pos_len >= 3) {
            n = pos_len / 3;
        }
        if (n == 0) n = 1;
        if (n > 4096) n = 4096;

        // Build instance data
        instances_.resize(n);
        int layout_mode = layout.int_value();
        float sp = spacing.value;

        bool use_custom_positions = (pos_data && pos_len >= n * 3);
        if (use_custom_positions) {
            for (uint32_t i = 0; i < n; ++i) {
                instances_[i].position[0] = pos_data[i * 3 + 0];
                instances_[i].position[1] = pos_data[i * 3 + 1];
                instances_[i].position[2] = pos_data[i * 3 + 2];
            }
        } else {
            switch (layout_mode) {
                case 1: { // Circle
                    for (uint32_t i = 0; i < n; ++i) {
                        float angle = 6.28318530718f * static_cast<float>(i) / static_cast<float>(n);
                        float radius = sp * static_cast<float>(n) / 6.28318530718f;
                        if (radius < sp) radius = sp;
                        instances_[i].position[0] = radius * std::cos(angle);
                        instances_[i].position[1] = 0.0f;
                        instances_[i].position[2] = radius * std::sin(angle);
                    }
                    break;
                }
                case 2: { // Line
                    float total = sp * static_cast<float>(n - 1);
                    float start = -total * 0.5f;
                    for (uint32_t i = 0; i < n; ++i) {
                        instances_[i].position[0] = start + sp * static_cast<float>(i);
                        instances_[i].position[1] = 0.0f;
                        instances_[i].position[2] = 0.0f;
                    }
                    break;
                }
                default: { // Grid
                    uint32_t cols = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(n))));
                    uint32_t rows = (n + cols - 1) / cols;
                    float ox = -static_cast<float>(cols - 1) * sp * 0.5f;
                    float oz = -static_cast<float>(rows - 1) * sp * 0.5f;
                    for (uint32_t i = 0; i < n; ++i) {
                        uint32_t col = i % cols;
                        uint32_t row = i / cols;
                        instances_[i].position[0] = ox + static_cast<float>(col) * sp;
                        instances_[i].position[1] = 0.0f;
                        instances_[i].position[2] = oz + static_cast<float>(row) * sp;
                    }
                    break;
                }
            }
        }

        // Scales
        bool use_custom_scales = (scale_data && scale_len >= n);
        for (uint32_t i = 0; i < n; ++i) {
            instances_[i].scale = use_custom_scales ? scale_data[i] : 1.0f;
        }

        // Colors
        bool use_custom_colors = (color_data && color_len >= n * 4);
        for (uint32_t i = 0; i < n; ++i) {
            if (use_custom_colors) {
                instances_[i].color[0] = color_data[i * 4 + 0];
                instances_[i].color[1] = color_data[i * 4 + 1];
                instances_[i].color[2] = color_data[i * 4 + 2];
                instances_[i].color[3] = color_data[i * 4 + 3];
            } else {
                instances_[i].color[0] = input->color[0];
                instances_[i].color[1] = input->color[1];
                instances_[i].color[2] = input->color[2];
                instances_[i].color[3] = input->color[3];
            }
        }

        // Upload to storage buffer
        uint32_t buf_size = n * sizeof(vivid::gpu::InstanceData3D);
        if (buf_size < 32) buf_size = 32;

        if (n != current_count_) {
            rebuild_storage(gpu, n, buf_size);
        }
        if (storage_buf_) {
            wgpuQueueWriteBuffer(gpu->queue, storage_buf_, 0,
                                 instances_.data(), n * sizeof(vivid::gpu::InstanceData3D));
        }

        // Output: shallow copy of input fragment with instance data
        fragment_ = *input;
        fragment_.instance_buffer = storage_buf_;
        fragment_.instance_count  = n;

        gpu->output_scene = &fragment_;
    }

    ~Instancer3D() override {
        vivid::gpu::release(storage_buf_);
    }

private:
    vivid::gpu::VividSceneFragment fragment_{};
    std::vector<vivid::gpu::InstanceData3D> instances_;
    WGPUBuffer storage_buf_   = nullptr;
    uint32_t   current_count_ = 0;

    void rebuild_storage(VividGpuState* gpu, uint32_t count, uint32_t buf_size) {
        vivid::gpu::release(storage_buf_);
        current_count_ = count;

        if (count == 0) return;

        WGPUBufferDescriptor desc{};
        desc.label = vivid_sv("Instancer3D Storage");
        desc.size  = buf_size;
        desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
        storage_buf_ = wgpuDeviceCreateBuffer(gpu->device, &desc);
    }
};

VIVID_REGISTER(Instancer3D)
