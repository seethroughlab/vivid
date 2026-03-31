// Test: GPU compute-backed lane evaluation (Phase E).
//
// Proves that a WGSL compute shader can process lane-bearing data and
// produce output identical to CPU evaluation. This is the strongest
// backend-independence claim: same operation, same input, GPU vs CPU,
// output matches within floating-point tolerance.
//
// The test creates a headless WebGPU device, uploads lane data to a
// storage buffer, dispatches a compute shader (multiply by 2), reads
// back the result, and compares against expected values.

#include "operator_api/gpu_common.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstring>

static int failures = 0;
static int skipped  = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static void check_float(float actual, float expected, float tol, const char* msg) {
    if (std::fabs(actual - expected) > tol) {
        std::fprintf(stderr, "  FAIL: %s (expected %.4f, got %.4f)\n", msg, expected, actual);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s (%.4f)\n", msg, actual);
    }
}

// ============================================================================
// Headless WebGPU init
// ============================================================================

struct HeadlessGpu {
    WGPUInstance instance = nullptr;
    WGPUAdapter  adapter  = nullptr;
    WGPUDevice   device   = nullptr;
    WGPUQueue    queue    = nullptr;

    bool init() {
        WGPUInstanceDescriptor inst_desc{};
        instance = wgpuCreateInstance(&inst_desc);
        if (!instance) return false;

        struct AdapterData { WGPUAdapter adapter = nullptr; bool done = false; };
        AdapterData ad;
        WGPURequestAdapterCallbackInfo acb{};
        acb.mode = WGPUCallbackMode_AllowSpontaneous;
        acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                          WGPUStringView, void* ud1, void*) {
            auto* d = static_cast<AdapterData*>(ud1);
            if (status == WGPURequestAdapterStatus_Success) d->adapter = adapter;
            d->done = true;
        };
        acb.userdata1 = &ad;
        WGPURequestAdapterOptions opts{};
        opts.powerPreference = WGPUPowerPreference_HighPerformance;
        wgpuInstanceRequestAdapter(instance, &opts, acb);
        if (!ad.done || !ad.adapter) return false;
        adapter = ad.adapter;

        struct DeviceData { WGPUDevice device = nullptr; bool done = false; };
        DeviceData dd;
        WGPURequestDeviceCallbackInfo dcb{};
        dcb.mode = WGPUCallbackMode_AllowSpontaneous;
        dcb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                          WGPUStringView, void* ud1, void*) {
            auto* d = static_cast<DeviceData*>(ud1);
            if (status == WGPURequestDeviceStatus_Success) d->device = device;
            d->done = true;
        };
        dcb.userdata1 = &dd;
        WGPUDeviceDescriptor dev_desc{};
        dev_desc.label = vivid_sv("Compute Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType, WGPUStringView, void*, void*) {};
        wgpuAdapterRequestDevice(adapter, &dev_desc, dcb);
        if (!dd.done || !dd.device) return false;
        device = dd.device;
        queue = wgpuDeviceGetQueue(device);
        return true;
    }

    void shutdown() {
        if (queue)    { wgpuQueueRelease(queue);    queue    = nullptr; }
        if (device)   { wgpuDeviceRelease(device);  device   = nullptr; }
        if (adapter)  { wgpuAdapterRelease(adapter); adapter = nullptr; }
        if (instance) { wgpuInstanceRelease(instance); instance = nullptr; }
    }
};

// ============================================================================
// Compute shader: multiply each lane by 2.0
// ============================================================================

static const char* kComputeShader = R"(
@group(0) @binding(0) var<storage, read> input_data: array<f32>;
@group(0) @binding(1) var<storage, read_write> output_data: array<f32>;

struct Params { lane_count: u32, pad0: u32, pad1: u32, pad2: u32 };
@group(0) @binding(2) var<uniform> params: Params;

@compute @workgroup_size(64)
fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let idx = gid.x;
    if (idx >= params.lane_count) { return; }
    output_data[idx] = input_data[idx] * 2.0;
}
)";

// ============================================================================
// Run compute dispatch and read back results
// ============================================================================

static bool run_compute_mul2(HeadlessGpu& gpu, const std::vector<float>& input,
                              std::vector<float>& output) {
    uint32_t lane_count = static_cast<uint32_t>(input.size());
    uint64_t data_size = lane_count * sizeof(float);
    output.resize(lane_count, 0.0f);

    // Create shader + pipeline
    WGPUShaderModule shader = vivid::gpu::create_compute_shader(gpu.device, kComputeShader, "mul2 shader");
    if (!shader) return false;

    // Bind group layout: storage(0, read), storage(1, read_write), uniform(2)
    WGPUBindGroupLayoutEntry bgl_entries[3] = {};
    bgl_entries[0].binding = 0;
    bgl_entries[0].visibility = WGPUShaderStage_Compute;
    bgl_entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
    bgl_entries[0].buffer.minBindingSize = data_size;

    bgl_entries[1].binding = 1;
    bgl_entries[1].visibility = WGPUShaderStage_Compute;
    bgl_entries[1].buffer.type = WGPUBufferBindingType_Storage;
    bgl_entries[1].buffer.minBindingSize = data_size;

    bgl_entries[2].binding = 2;
    bgl_entries[2].visibility = WGPUShaderStage_Compute;
    bgl_entries[2].buffer.type = WGPUBufferBindingType_Uniform;
    bgl_entries[2].buffer.minBindingSize = 16;

    WGPUBindGroupLayoutDescriptor bgl_desc{};
    bgl_desc.label = vivid_sv("mul2 bgl");
    bgl_desc.entryCount = 3;
    bgl_desc.entries = bgl_entries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(gpu.device, &bgl_desc);

    WGPUPipelineLayoutDescriptor pl_desc{};
    pl_desc.label = vivid_sv("mul2 pl");
    pl_desc.bindGroupLayoutCount = 1;
    pl_desc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipe_layout = wgpuDeviceCreatePipelineLayout(gpu.device, &pl_desc);

    WGPUComputePipeline pipeline = vivid::gpu::create_compute_pipeline(
        gpu.device, shader, pipe_layout, "mul2 pipeline");
    if (!pipeline) {
        wgpuShaderModuleRelease(shader);
        wgpuBindGroupLayoutRelease(bgl);
        wgpuPipelineLayoutRelease(pipe_layout);
        return false;
    }

    // Create buffers
    WGPUBuffer input_buf = vivid::gpu::create_storage_buffer(gpu.device, data_size, "input");
    WGPUBuffer output_buf = vivid::gpu::create_storage_buffer(gpu.device, data_size, "output");
    WGPUBuffer readback_buf = vivid::gpu::create_readback_buffer(gpu.device, data_size, "readback");
    WGPUBuffer uniform_buf = vivid::gpu::create_uniform_buffer(gpu.device, 16, "params");

    // Upload input data
    wgpuQueueWriteBuffer(gpu.queue, input_buf, 0, input.data(), data_size);

    // Upload params (lane_count + padding)
    uint32_t params[4] = { lane_count, 0, 0, 0 };
    wgpuQueueWriteBuffer(gpu.queue, uniform_buf, 0, params, 16);

    // Create bind group
    WGPUBindGroupEntry bg_entries[3] = {};
    bg_entries[0].binding = 0;
    bg_entries[0].buffer = input_buf;
    bg_entries[0].size = data_size;
    bg_entries[1].binding = 1;
    bg_entries[1].buffer = output_buf;
    bg_entries[1].size = data_size;
    bg_entries[2].binding = 2;
    bg_entries[2].buffer = uniform_buf;
    bg_entries[2].size = 16;

    WGPUBindGroupDescriptor bg_desc{};
    bg_desc.label = vivid_sv("mul2 bg");
    bg_desc.layout = bgl;
    bg_desc.entryCount = 3;
    bg_desc.entries = bg_entries;
    WGPUBindGroup bind_group = wgpuDeviceCreateBindGroup(gpu.device, &bg_desc);

    // Encode: dispatch compute + copy output → readback
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid_sv("mul2 encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    uint32_t workgroups = (lane_count + 63) / 64;
    vivid::gpu::dispatch_compute(encoder, pipeline, bind_group, workgroups, "mul2 dispatch");

    wgpuCommandEncoderCopyBufferToBuffer(encoder, output_buf, 0, readback_buf, 0, data_size);

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid_sv("mul2 cmd");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu.queue, 1, &cmd);

    // Map readback buffer (synchronous busy-wait)
    struct MapData { bool done = false; WGPUMapAsyncStatus status = {}; };
    MapData md;
    WGPUBufferMapCallbackInfo map_cb{};
    map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
    map_cb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ud1, void*) {
        auto* d = static_cast<MapData*>(ud1);
        d->status = status;
        d->done = true;
    };
    map_cb.userdata1 = &md;
    wgpuBufferMapAsync(readback_buf, WGPUMapMode_Read, 0, data_size, map_cb);

    // Poll until mapped
    while (!md.done)
        wgpuDevicePoll(gpu.device, true, nullptr);

    bool ok = (md.status == WGPUMapAsyncStatus_Success);
    if (ok) {
        const float* mapped = static_cast<const float*>(
            wgpuBufferGetConstMappedRange(readback_buf, 0, data_size));
        if (mapped)
            std::memcpy(output.data(), mapped, data_size);
        else
            ok = false;
        wgpuBufferUnmap(readback_buf);
    }

    // Cleanup
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
    wgpuBindGroupRelease(bind_group);
    wgpuBufferRelease(uniform_buf);
    wgpuBufferRelease(readback_buf);
    wgpuBufferRelease(output_buf);
    wgpuBufferRelease(input_buf);
    wgpuComputePipelineRelease(pipeline);
    wgpuPipelineLayoutRelease(pipe_layout);
    wgpuBindGroupLayoutRelease(bgl);
    wgpuShaderModuleRelease(shader);

    return ok;
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::fprintf(stderr, "\n=== test_compute_lane_equivalence ===\n\n");

    HeadlessGpu gpu;
    if (!gpu.init()) {
        std::fprintf(stderr, "  SKIP: headless GPU init failed (no GPU available)\n");
        return 0;  // skip, not fail
    }
    check(true, "headless GPU initialized");

    // --- Test 1: 64-lane compute dispatch ---
    std::fprintf(stderr, "\n--- 64-lane GPU compute (multiply by 2) ---\n");
    {
        constexpr uint32_t N = 64;
        std::vector<float> input(N);
        for (uint32_t i = 0; i < N; ++i)
            input[i] = static_cast<float>(i + 1);  // [1, 2, ..., 64]

        std::vector<float> output;
        bool ok = run_compute_mul2(gpu, input, output);
        check(ok, "compute dispatch succeeded");

        if (ok) {
            check(output.size() == N, "output has 64 elements");
            // Spot-check: output[i] = input[i] * 2 = (i+1) * 2
            check_float(output[0],  2.0f,   1e-5f, "lane 0: 1*2 = 2");
            check_float(output[31], 64.0f,  1e-5f, "lane 31: 32*2 = 64");
            check_float(output[63], 128.0f, 1e-5f, "lane 63: 64*2 = 128");

            // Full comparison
            bool all_match = true;
            for (uint32_t i = 0; i < N; ++i) {
                float expected = static_cast<float>(i + 1) * 2.0f;
                if (std::fabs(output[i] - expected) > 1e-5f) {
                    all_match = false;
                    std::fprintf(stderr, "  FAIL: lane %u: expected %.4f, got %.4f\n",
                                 i, expected, output[i]);
                    failures++;
                    break;
                }
            }
            if (all_match) check(true, "all 64 lanes match expected values");
        }
    }

    // --- Test 2: 512-lane compute dispatch (scale test) ---
    std::fprintf(stderr, "\n--- 512-lane GPU compute (scale test) ---\n");
    {
        constexpr uint32_t N = 512;
        std::vector<float> input(N);
        for (uint32_t i = 0; i < N; ++i)
            input[i] = static_cast<float>(i + 1) * 0.1f;

        std::vector<float> output;
        bool ok = run_compute_mul2(gpu, input, output);
        check(ok, "512-lane compute dispatch succeeded");

        if (ok) {
            check(output.size() == N, "output has 512 elements");
            check_float(output[0],   0.2f,   1e-5f, "lane 0: 0.1*2 = 0.2");
            check_float(output[255], 51.2f,  1e-3f, "lane 255: 25.6*2 = 51.2");
            check_float(output[511], 102.4f, 1e-3f, "lane 511: 51.2*2 = 102.4");
        }
    }

    gpu.shutdown();

    std::fprintf(stderr, "\n%s (%d failures, %d skipped)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures, skipped);
    return failures > 0 ? 1 : 0;
}
