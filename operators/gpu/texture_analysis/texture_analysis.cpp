// TextureAnalysis — GPU operator that reads back a small center crop of its
// input texture and produces scalar analysis metrics as CONTROL_FLOAT outputs.
//
// This is the GPU→Control readback bridge: it enables GPU-computed visuals to
// drive audio parameters, control logic, or any other downstream operator.
//
// Strategy:
//   - Passthrough: copy input texture → output texture (so it can be inserted
//     into any GPU chain without breaking it).
//   - Readback: use a separate command encoder to copy a small center crop from
//     the input texture (which has LAST frame's data) to a staging buffer,
//     synchronously wait, map, and compute metrics. For a 16×16 crop this is
//     ~2KB and completes in <1ms.
//   - 1-frame latency on analysis is acceptable and matches the architecture
//     doc ("GPU→Control: 1–2 frames").

#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"
#include <webgpu/wgpu.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>

// Half-float (IEEE 754 binary16) to float conversion
static float half_to_float(uint16_t h) {
    uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp  = (h >> 10) & 0x1F;
    uint32_t mant = h & 0x03FF;
    uint32_t f;
    if (exp == 0) {
        if (mant == 0) { f = sign; }
        else {
            exp = 1;
            while (!(mant & 0x0400)) { mant <<= 1; exp--; }
            mant &= 0x03FF;
            f = sign | ((exp + 112) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        f = sign | 0x7F800000u | (mant << 13);
    } else {
        f = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float result;
    std::memcpy(&result, &f, sizeof(float));
    return result;
}

static constexpr uint32_t kCropSize = 16;  // 16×16 center crop

struct TextureAnalysis : vivid::OperatorBase {
    static constexpr const char* kName   = "TextureAnalysis";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int> skip_frames{"skip_frames", 1, {"Every frame", "Every 2nd", "Every 4th", "Every 8th"}};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&skip_frames);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});  // passthrough
        out.push_back({"brightness", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"contrast",   VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"red",        VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"green",      VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"blue",       VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"edge_density", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu) return;

        // Resolve input texture from the gpu state
        WGPUTexture input_tex = nullptr;
        uint32_t input_w = 0, input_h = 0;
        if (gpu->input_textures && gpu->input_texture_count > 0) {
            input_tex = gpu->input_textures[0];
            input_w = gpu->input_texture_widths ? gpu->input_texture_widths[0] : 0;
            input_h = gpu->input_texture_heights ? gpu->input_texture_heights[0] : 0;
        }

        // --- Passthrough: copy input texture → output texture ---
        if (input_tex && input_w > 0 && input_h > 0) {
            // Request same size as input for passthrough
            auto* mutable_ctx = const_cast<VividProcessContext*>(ctx);
            mutable_ctx->preferred_tex_width  = input_w;
            mutable_ctx->preferred_tex_height = input_h;

            if (input_w == gpu->output_width && input_h == gpu->output_height) {
                WGPUTexelCopyTextureInfo src{};
                src.texture = input_tex;
                src.mipLevel = 0;
                src.origin = { 0, 0, 0 };
                src.aspect = WGPUTextureAspect_All;

                WGPUTexelCopyTextureInfo dst{};
                dst.texture = gpu->output_texture;
                dst.mipLevel = 0;
                dst.origin = { 0, 0, 0 };
                dst.aspect = WGPUTextureAspect_All;

                WGPUExtent3D size = { gpu->output_width, gpu->output_height, 1 };
                wgpuCommandEncoderCopyTextureToTexture(gpu->command_encoder, &src, &dst, &size);
            }
        }

        // --- Analysis: readback center crop from input texture ---
        // Skip frames based on parameter to reduce GPU stalls
        uint32_t skip = 1u << skip_frames.int_value();
        frame_counter_++;
        if (frame_counter_ % skip != 0) {
            write_outputs(ctx);
            return;
        }

        if (!input_tex || input_w == 0 || input_h == 0) {
            write_outputs(ctx);
            return;
        }

        // Determine crop region (center of texture)
        uint32_t crop_w = std::min(kCropSize, input_w);
        uint32_t crop_h = std::min(kCropSize, input_h);
        uint32_t crop_x = (input_w - crop_w) / 2;
        uint32_t crop_y = (input_h - crop_h) / 2;

        readback_center_crop(gpu, input_tex, crop_x, crop_y, crop_w, crop_h);
        write_outputs(ctx);
    }

    ~TextureAnalysis() override {
        if (staging_buf_) wgpuBufferRelease(staging_buf_);
    }

private:
    // Cached analysis results
    float brightness_ = 0.0f;
    float contrast_   = 0.0f;
    float red_        = 0.0f;
    float green_      = 0.0f;
    float blue_       = 0.0f;
    float edge_density_ = 0.0f;

    // GPU readback state
    WGPUBuffer staging_buf_ = nullptr;
    uint64_t staging_buf_size_ = 0;
    uint32_t frame_counter_ = 0;

    void write_outputs(const VividProcessContext* ctx) {
        // Output port indices (CONTROL_FLOAT only, texture ports don't use output_values):
        // brightness(0), contrast(1), red(2), green(3), blue(4), edge_density(5)
        ctx->output_values[0] = brightness_;
        ctx->output_values[1] = contrast_;
        ctx->output_values[2] = red_;
        ctx->output_values[3] = green_;
        ctx->output_values[4] = blue_;
        ctx->output_values[5] = edge_density_;
    }

    void readback_center_crop(VividGpuState* gpu, WGPUTexture input_tex,
                               uint32_t crop_x, uint32_t crop_y,
                               uint32_t crop_w, uint32_t crop_h) {
        // RGBA16Float = 8 bytes per pixel
        static constexpr uint32_t kBpp = 8;
        static constexpr uint32_t kGpuRowAlign = 256;
        uint32_t unpadded_row = crop_w * kBpp;
        uint32_t aligned_row = (unpadded_row + kGpuRowAlign - 1) & ~(kGpuRowAlign - 1);
        uint64_t buf_size = static_cast<uint64_t>(aligned_row) * crop_h;

        // Create or resize staging buffer
        if (!staging_buf_ || staging_buf_size_ < buf_size) {
            if (staging_buf_) wgpuBufferRelease(staging_buf_);
            WGPUBufferDescriptor desc{};
            desc.label = vivid_sv("TextureAnalysis Staging");
            desc.size = buf_size;
            desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            desc.mappedAtCreation = false;
            staging_buf_ = wgpuDeviceCreateBuffer(gpu->device, &desc);
            staging_buf_size_ = buf_size;
            if (!staging_buf_) return;
        }

        // Create a separate encoder for the copy (reads last frame's data)
        WGPUCommandEncoderDescriptor enc_desc{};
        enc_desc.label = vivid_sv("TextureAnalysis Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu->device, &enc_desc);

        WGPUTexelCopyTextureInfo src{};
        src.texture = input_tex;
        src.mipLevel = 0;
        src.origin = { crop_x, crop_y, 0 };
        src.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo dst{};
        dst.buffer = staging_buf_;
        dst.layout.offset = 0;
        dst.layout.bytesPerRow = aligned_row;
        dst.layout.rowsPerImage = crop_h;

        WGPUExtent3D copy_size = { crop_w, crop_h, 1 };
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copy_size);

        WGPUCommandBufferDescriptor cmd_desc{};
        cmd_desc.label = vivid_sv("TextureAnalysis Commands");
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
        wgpuQueueSubmit(gpu->queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);
        wgpuCommandEncoderRelease(encoder);

        // Wait for GPU work
        {
            bool work_done = false;
            WGPUQueueWorkDoneCallbackInfo work_cb{};
            work_cb.mode = WGPUCallbackMode_AllowSpontaneous;
            work_cb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
                *static_cast<bool*>(ud1) = true;
            };
            work_cb.userdata1 = &work_done;
            wgpuQueueOnSubmittedWorkDone(gpu->queue, work_cb);
            while (!work_done)
                wgpuDevicePoll(gpu->device, true, nullptr);
        }

        // Map staging buffer
        bool map_done = false;
        WGPUBufferMapCallbackInfo map_cb{};
        map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
        map_cb.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* ud1, void*) {
            *static_cast<bool*>(ud1) = true;
        };
        map_cb.userdata1 = &map_done;
        wgpuBufferMapAsync(staging_buf_, WGPUMapMode_Read, 0, buf_size, map_cb);
        while (!map_done)
            wgpuDevicePoll(gpu->device, true, nullptr);

        const uint8_t* mapped = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(staging_buf_, 0, buf_size));
        if (!mapped) {
            wgpuBufferUnmap(staging_buf_);
            return;
        }

        // Decode RGBA16Float pixels and compute metrics
        uint32_t num_pixels = crop_w * crop_h;
        float sum_r = 0, sum_g = 0, sum_b = 0, sum_lum = 0;
        std::vector<float> luminances(num_pixels);

        for (uint32_t y = 0; y < crop_h; ++y) {
            const uint8_t* row = mapped + y * aligned_row;
            for (uint32_t x = 0; x < crop_w; ++x) {
                const uint16_t* fp16 = reinterpret_cast<const uint16_t*>(row + x * kBpp);
                float r = std::max(0.0f, std::min(1.0f, half_to_float(fp16[0])));
                float g = std::max(0.0f, std::min(1.0f, half_to_float(fp16[1])));
                float b = std::max(0.0f, std::min(1.0f, half_to_float(fp16[2])));
                float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;

                sum_r += r;
                sum_g += g;
                sum_b += b;
                sum_lum += lum;
                luminances[y * crop_w + x] = lum;
            }
        }

        float inv_n = 1.0f / static_cast<float>(num_pixels);
        brightness_ = sum_lum * inv_n;
        red_   = sum_r * inv_n;
        green_ = sum_g * inv_n;
        blue_  = sum_b * inv_n;

        // Contrast: standard deviation of luminance
        float sum_sq_diff = 0;
        for (uint32_t i = 0; i < num_pixels; ++i) {
            float diff = luminances[i] - brightness_;
            sum_sq_diff += diff * diff;
        }
        contrast_ = std::sqrt(sum_sq_diff * inv_n);

        // Edge density: simplified Sobel magnitude on luminance
        float edge_sum = 0;
        uint32_t edge_count = 0;
        for (uint32_t y = 1; y + 1 < crop_h; ++y) {
            for (uint32_t x = 1; x + 1 < crop_w; ++x) {
                auto L = [&](uint32_t px, uint32_t py) {
                    return luminances[py * crop_w + px];
                };
                // Sobel X
                float gx = -L(x-1,y-1) + L(x+1,y-1)
                         - 2*L(x-1,y)  + 2*L(x+1,y)
                         - L(x-1,y+1)  + L(x+1,y+1);
                // Sobel Y
                float gy = -L(x-1,y-1) - 2*L(x,y-1) - L(x+1,y-1)
                         + L(x-1,y+1) + 2*L(x,y+1) + L(x+1,y+1);
                edge_sum += std::sqrt(gx * gx + gy * gy);
                edge_count++;
            }
        }
        edge_density_ = (edge_count > 0) ? (edge_sum / static_cast<float>(edge_count)) : 0.0f;
        // Normalize to 0-1 range (max Sobel magnitude for unit step is ~4.24)
        edge_density_ = std::min(1.0f, edge_density_ / 4.0f);

        wgpuBufferUnmap(staging_buf_);
    }
};

VIVID_REGISTER(TextureAnalysis)
