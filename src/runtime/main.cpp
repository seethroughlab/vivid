#include "runtime/gpu_context.h"
#include "runtime/operator_loader.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <vector>

// #16191D in sRGB → linear: pow(x/255, 2.2)
static constexpr double kClearLinear[4]  = { 0.00699, 0.00821, 0.01041, 1.0 };
// #16191D as raw unorm (no gamma conversion)
static constexpr double kClearRaw[4]     = { 0.0863, 0.0980, 0.1137, 1.0 };

static bool is_srgb_format(WGPUTextureFormat fmt) {
    switch (fmt) {
        case WGPUTextureFormat_RGBA8UnormSrgb:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return true;
        default:
            return false;
    }
}

static WGPUStringView to_sv(const char* s) {
    return { s, s ? std::strlen(s) : 0 };
}

int main() {
    // --- GLFW ---
    if (!glfwInit()) {
        std::fprintf(stderr, "[vivid] Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Vivid", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[vivid] Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    // --- GPU ---
    vivid::GpuContext gpu;
    if (!gpu.init(window, 1280, 800)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Pick clear color based on surface format
    const double* clear = is_srgb_format(gpu.surface_format()) ? kClearLinear : kClearRaw;

    // --- Load LFO operator ---
    vivid::OperatorLoader lfo_loader;
    void* lfo_instance = nullptr;
    std::vector<float> param_values;
    std::vector<float> output_values;
    uint64_t frame_count = 0;

    if (lfo_loader.load("lfo.dylib")) {
        const VividOperatorDescriptor* desc = lfo_loader.descriptor();
        std::fprintf(stderr, "[vivid] Loaded operator: lfo.dylib\n");
        std::fprintf(stderr, "[vivid] Operator: %s (domain=%d)\n", desc->name, desc->domain);
        std::fprintf(stderr, "[vivid] Parameters (%u):\n", desc->param_count);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            std::fprintf(stderr, "  [%u] %s: default=%.3f min=%.3f max=%.3f\n",
                i, desc->params[i].name,
                desc->params[i].default_value,
                desc->params[i].min_value,
                desc->params[i].max_value);
        }
        std::fprintf(stderr, "[vivid] Ports (%u):\n", desc->port_count);
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            std::fprintf(stderr, "  [%u] %s: type=%d dir=%d\n",
                i, desc->ports[i].name,
                desc->ports[i].type,
                desc->ports[i].direction);
        }

        // Init param values from defaults
        param_values.resize(desc->param_count);
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            param_values[i] = desc->params[i].default_value;
        }

        // Allocate output values
        output_values.resize(desc->port_count, 0.0f);

        lfo_instance = lfo_loader.create_instance();
    } else {
        std::fprintf(stderr, "[vivid] LFO load failed (non-fatal, continuing)\n");
    }

    double prev_time = glfwGetTime();

    // --- Main loop ---
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // --- Tick LFO ---
        if (lfo_instance) {
            double now = glfwGetTime();
            VividProcessContext pctx{};
            pctx.time          = now;
            pctx.delta_time    = now - prev_time;
            pctx.frame         = frame_count;
            pctx.param_values  = param_values.data();
            pctx.output_values = output_values.data();
            prev_time = now;

            lfo_loader.process(lfo_instance, &pctx);

            if (frame_count % 60 == 0) {
                std::fprintf(stderr, "[vivid] LFO frame=%llu time=%.2f value=%.4f\n",
                    static_cast<unsigned long long>(frame_count), now, output_values[0]);
            }
            frame_count++;
        }

        vivid::FrameState frame;
        if (!gpu.begin_frame(frame))
            continue;

        // Render pass — clear only
        WGPURenderPassColorAttachment color_att{};
        color_att.nextInChain = nullptr;
        color_att.view = frame.view;
        color_att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color_att.resolveTarget = nullptr;
        color_att.loadOp = WGPULoadOp_Clear;
        color_att.storeOp = WGPUStoreOp_Store;
        color_att.clearValue = { clear[0], clear[1], clear[2], clear[3] };

        WGPURenderPassDescriptor rp_desc{};
        rp_desc.nextInChain = nullptr;
        rp_desc.label = to_sv("Clear Pass");
        rp_desc.colorAttachmentCount = 1;
        rp_desc.colorAttachments = &color_att;
        rp_desc.depthStencilAttachment = nullptr;
        rp_desc.timestampWrites = nullptr;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        gpu.end_frame(frame);

        // wgpu-native: poll the device to process async operations
        wgpuDevicePoll(gpu.device(), false, nullptr);
    }

    // --- Shutdown ---
    if (lfo_instance) {
        lfo_loader.destroy_instance(lfo_instance);
        lfo_instance = nullptr;
    }
    // lfo_loader destructor handles dlclose

    gpu.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();

    std::fprintf(stderr, "[vivid] Clean shutdown\n");
    return 0;
}
