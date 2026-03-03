#include "operator_api/gpu_3d.h"
#include "operator_api/gpu_operator.h"
#include "runtime/operator_registry.h"
#include "runtime/graph.h"
#include "runtime/scheduler.h"
#include "common/gpu_util.h"
#include "ui/node_graph_util.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <filesystem>

// ============================================================================
// Test infrastructure
// ============================================================================

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

static void skip(const char* msg) {
    std::fprintf(stderr, "  SKIP: %s\n", msg);
    skipped++;
}

// ============================================================================
// Headless WebGPU init (same as test_render_3d)
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
        dev_desc.label = vivid::to_sv("Shape3D Test Device");
        dev_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        dev_desc.deviceLostCallbackInfo.callback =
            [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView, void*, void*) {};
        dev_desc.uncapturedErrorCallbackInfo.callback =
            [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
                std::fprintf(stderr, "[shape3d_test] WebGPU error (%d): %.*s\n",
                             static_cast<int>(type), static_cast<int>(msg.length),
                             msg.data ? msg.data : "");
            };
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
// GPU readback utility
// ============================================================================

static const uint32_t kRowAlignment = 256;

static uint32_t aligned_bytes_per_row(uint32_t width) {
    uint32_t unpadded = width * 4;
    return (unpadded + kRowAlignment - 1) & ~(kRowAlignment - 1);
}

static std::vector<uint8_t> readback_texture(WGPUDevice device, WGPUQueue queue,
                                              WGPUTexture texture,
                                              uint32_t width, uint32_t height) {
    uint32_t padded_row = aligned_bytes_per_row(width);
    uint64_t buf_size = static_cast<uint64_t>(padded_row) * height;

    WGPUBufferDescriptor buf_desc{};
    buf_desc.label = vivid::to_sv("Readback Buffer");
    buf_desc.size  = buf_size;
    buf_desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer staging = wgpuDeviceCreateBuffer(device, &buf_desc);

    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Readback Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);

    WGPUTexelCopyTextureInfo src{};
    src.texture = texture;
    src.aspect  = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst{};
    dst.buffer = staging;
    dst.layout.bytesPerRow = padded_row;
    dst.layout.rowsPerImage = height;
    WGPUExtent3D extent = { width, height, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);

    WGPUCommandBufferDescriptor cmd_desc{};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    struct WorkDone { bool done = false; };
    WorkDone wd;
    WGPUQueueWorkDoneCallbackInfo wcb{};
    wcb.mode = WGPUCallbackMode_AllowSpontaneous;
    wcb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
        static_cast<WorkDone*>(ud1)->done = true;
    };
    wcb.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(queue, wcb);
    while (!wd.done) wgpuDevicePoll(device, true, nullptr);

    struct MapData { bool done = false; WGPUMapAsyncStatus status; };
    MapData md;
    WGPUBufferMapCallbackInfo mcb{};
    mcb.mode = WGPUCallbackMode_AllowSpontaneous;
    mcb.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ud1, void*) {
        auto* d = static_cast<MapData*>(ud1);
        d->status = status;
        d->done = true;
    };
    mcb.userdata1 = &md;
    wgpuBufferMapAsync(staging, WGPUMapMode_Read, 0, buf_size, mcb);
    while (!md.done) wgpuDevicePoll(device, true, nullptr);

    std::vector<uint8_t> pixels;
    if (md.status == WGPUMapAsyncStatus_Success) {
        const uint8_t* mapped = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(staging, 0, buf_size));
        uint32_t dense_row = width * 4;
        pixels.resize(static_cast<size_t>(dense_row) * height);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(pixels.data() + y * dense_row, mapped + y * padded_row, dense_row);
        wgpuBufferUnmap(staging);
    }
    wgpuBufferRelease(staging);
    return pixels;
}

// Tick scheduler with GPU state and submit
static void tick_and_submit(vivid::Scheduler& sched, HeadlessGpu& gpu,
                            WGPUTextureFormat format) {
    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.label = vivid::to_sv("Tick Encoder");
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gpu.device, &enc_desc);

    VividGpuState gpu_state{};
    gpu_state.device          = gpu.device;
    gpu_state.queue           = gpu.queue;
    gpu_state.command_encoder = encoder;
    gpu_state.output_format   = format;

    sched.tick(0.0, 0.016, 0, &gpu_state);

    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.label = vivid::to_sv("Tick Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuQueueSubmit(gpu.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    struct WorkDone { bool done = false; };
    WorkDone wd;
    WGPUQueueWorkDoneCallbackInfo wcb{};
    wcb.mode = WGPUCallbackMode_AllowSpontaneous;
    wcb.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
        static_cast<WorkDone*>(ud1)->done = true;
    };
    wcb.userdata1 = &wd;
    wgpuQueueOnSubmittedWorkDone(gpu.queue, wcb);
    while (!wd.done) wgpuDevicePoll(gpu.device, true, nullptr);
}

// ============================================================================
// CPU geometry generators (duplicated for test validation — these mirror
// the operator's generators so we can validate vertex/index counts & normals
// without needing GPU)
// ============================================================================

struct TestVertex { float pos[3]; float normal[3]; float tangent[4]; float uv[2]; };

static void test_gen_cube(std::vector<TestVertex>& v, std::vector<uint32_t>& idx) {
    v.clear(); idx.clear();
    struct Face { float nx,ny,nz; float tx,ty,tz; float p[4][3]; };
    static const Face faces[] = {
        { 0,0,1,  1,0,0,  { {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f} }},
        { 0,0,-1, -1,0,0, { {0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{0.5f,0.5f,-0.5f} }},
        { 1,0,0,  0,0,-1, { {0.5f,-0.5f,0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{0.5f,0.5f,0.5f} }},
        {-1,0,0,  0,0,1,  { {-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,0.5f},{-0.5f,0.5f,0.5f},{-0.5f,0.5f,-0.5f} }},
        { 0,1,0,  1,0,0,  { {-0.5f,0.5f,0.5f},{0.5f,0.5f,0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f} }},
        { 0,-1,0, 1,0,0,  { {-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,-0.5f,0.5f},{-0.5f,-0.5f,0.5f} }},
    };
    static const float uvs[4][2] = {{0,0},{1,0},{1,1},{0,1}};
    for (int f = 0; f < 6; ++f) {
        auto base = static_cast<uint32_t>(v.size());
        for (int i = 0; i < 4; ++i) {
            TestVertex tv{};
            tv.pos[0] = faces[f].p[i][0]; tv.pos[1] = faces[f].p[i][1]; tv.pos[2] = faces[f].p[i][2];
            tv.normal[0] = faces[f].nx; tv.normal[1] = faces[f].ny; tv.normal[2] = faces[f].nz;
            tv.tangent[0] = faces[f].tx; tv.tangent[1] = faces[f].ty; tv.tangent[2] = faces[f].tz;
            tv.tangent[3] = 1.0f;
            tv.uv[0] = uvs[i][0]; tv.uv[1] = uvs[i][1];
            v.push_back(tv);
        }
        idx.push_back(base+0); idx.push_back(base+1); idx.push_back(base+2);
        idx.push_back(base+0); idx.push_back(base+2); idx.push_back(base+3);
    }
}

static void test_gen_sphere(std::vector<TestVertex>& v, std::vector<uint32_t>& idx, int detail) {
    v.clear(); idx.clear();
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTau = 6.28318530717958647692f;
    int stacks = detail / 2;
    int slices = detail;
    if (stacks < 2) stacks = 2;
    if (slices < 3) slices = 3;
    float radius = 0.5f;
    for (int i = 0; i <= stacks; ++i) {
        float phi = kPi * float(i) / float(stacks);
        float sp = std::sin(phi), cp = std::cos(phi);
        for (int j = 0; j <= slices; ++j) {
            float theta = kTau * float(j) / float(slices);
            float st = std::sin(theta), ct = std::cos(theta);
            TestVertex tv{};
            tv.normal[0] = sp * ct; tv.normal[1] = cp; tv.normal[2] = sp * st;
            tv.pos[0] = radius * tv.normal[0];
            tv.pos[1] = radius * tv.normal[1];
            tv.pos[2] = radius * tv.normal[2];
            if (sp > 1e-6f) {
                tv.tangent[0] = -st; tv.tangent[1] = 0.0f; tv.tangent[2] = ct;
            } else {
                tv.tangent[0] = 1.0f; tv.tangent[1] = 0.0f; tv.tangent[2] = 0.0f;
            }
            tv.tangent[3] = 1.0f;
            tv.uv[0] = float(j) / float(slices);
            tv.uv[1] = float(i) / float(stacks);
            v.push_back(tv);
        }
    }
    for (int i = 0; i < stacks; ++i) {
        for (int j = 0; j < slices; ++j) {
            uint32_t a = uint32_t(i * (slices+1) + j);
            uint32_t b = a + 1;
            uint32_t c = uint32_t((i+1) * (slices+1) + j);
            uint32_t d = c + 1;
            idx.push_back(a); idx.push_back(c); idx.push_back(d);
            idx.push_back(a); idx.push_back(d); idx.push_back(b);
        }
    }
}

static void test_gen_cone(std::vector<TestVertex>& v, std::vector<uint32_t>& idx, int detail) {
    v.clear(); idx.clear();
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTau = 6.28318530717958647692f;
    int slices = detail;
    if (slices < 3) slices = 3;
    float radius = 0.5f, half_h = 0.5f, height = 1.0f;
    float slant_len = std::sqrt(radius*radius + height*height);
    float ny_slant = radius / slant_len;
    float nh_slant = height / slant_len;

    // Body: per-slice triangles
    for (int j = 0; j < slices; ++j) {
        float t0 = kTau * float(j) / float(slices);
        float t1 = kTau * float(j+1) / float(slices);
        float tm = (t0+t1)*0.5f;
        float ct0 = std::cos(t0), st0 = std::sin(t0);
        float ct1 = std::cos(t1), st1 = std::sin(t1);
        float ctm = std::cos(tm), stm = std::sin(tm);
        float nx = nh_slant*ctm, nz = nh_slant*stm;
        float tx = -stm, tz = ctm;
        auto base = static_cast<uint32_t>(v.size());
        TestVertex apex{}; apex.pos[1] = half_h;
        apex.normal[0]=nx; apex.normal[1]=ny_slant; apex.normal[2]=nz;
        apex.tangent[0]=tx; apex.tangent[2]=tz; apex.tangent[3]=1;
        v.push_back(apex);
        TestVertex b0{}; b0.pos[0]=radius*ct0; b0.pos[1]=-half_h; b0.pos[2]=radius*st0;
        b0.normal[0]=nx; b0.normal[1]=ny_slant; b0.normal[2]=nz;
        b0.tangent[0]=tx; b0.tangent[2]=tz; b0.tangent[3]=1;
        v.push_back(b0);
        TestVertex b1{}; b1.pos[0]=radius*ct1; b1.pos[1]=-half_h; b1.pos[2]=radius*st1;
        b1.normal[0]=nx; b1.normal[1]=ny_slant; b1.normal[2]=nz;
        b1.tangent[0]=tx; b1.tangent[2]=tz; b1.tangent[3]=1;
        v.push_back(b1);
        idx.push_back(base); idx.push_back(base+1); idx.push_back(base+2);
    }
    // Bottom cap
    auto center = static_cast<uint32_t>(v.size());
    TestVertex cv{}; cv.pos[1] = -half_h;
    cv.normal[1] = -1; cv.tangent[0] = 1; cv.tangent[3] = 1;
    v.push_back(cv);
    auto ring_start = static_cast<uint32_t>(v.size());
    for (int j = 0; j <= slices; ++j) {
        float theta = kTau * float(j) / float(slices);
        float ct = std::cos(theta), st = std::sin(theta);
        TestVertex rv{}; rv.pos[0]=radius*ct; rv.pos[1]=-half_h; rv.pos[2]=radius*st;
        rv.normal[1]=-1; rv.tangent[0]=ct; rv.tangent[2]=st; rv.tangent[3]=1;
        v.push_back(rv);
    }
    for (int j = 0; j < slices; ++j) {
        idx.push_back(center);
        idx.push_back(ring_start + uint32_t(j+1));
        idx.push_back(ring_start + uint32_t(j));
    }
}

static void test_gen_pyramid(std::vector<TestVertex>& v, std::vector<uint32_t>& idx) {
    v.clear(); idx.clear();
    float h = 0.5f;
    float apex[3] = {0,h,0};
    float c0[3] = {-h,-h,-h}, c1[3] = {h,-h,-h}, c2[3] = {h,-h,h}, c3[3] = {-h,-h,h};
    auto cross_norm = [](const float a[3], const float b[3], const float c[3], float out[3]) {
        float e1[3]={b[0]-a[0],b[1]-a[1],b[2]-a[2]};
        float e2[3]={c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        out[0]=e1[1]*e2[2]-e1[2]*e2[1];
        out[1]=e1[2]*e2[0]-e1[0]*e2[2];
        out[2]=e1[0]*e2[1]-e1[1]*e2[0];
        float len=std::sqrt(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]);
        if(len>1e-8f){out[0]/=len;out[1]/=len;out[2]/=len;}
    };
    auto add_tri = [&](const float p0[3], const float p1[3], const float p2[3],
                       const float n[3], const float t[3]) {
        auto base = static_cast<uint32_t>(v.size());
        for (int i=0;i<3;++i) {
            TestVertex tv{};
            const float*p=(i==0)?p0:(i==1)?p1:p2;
            tv.pos[0]=p[0];tv.pos[1]=p[1];tv.pos[2]=p[2];
            tv.normal[0]=n[0];tv.normal[1]=n[1];tv.normal[2]=n[2];
            tv.tangent[0]=t[0];tv.tangent[1]=t[1];tv.tangent[2]=t[2];tv.tangent[3]=1;
            v.push_back(tv);
        }
        idx.push_back(base);idx.push_back(base+1);idx.push_back(base+2);
    };
    {float n[3];cross_norm(apex,c1,c0,n);float t[3]={1,0,0};add_tri(apex,c1,c0,n,t);}
    {float n[3];cross_norm(apex,c2,c1,n);float t[3]={0,0,-1};add_tri(apex,c2,c1,n,t);}
    {float n[3];cross_norm(apex,c3,c2,n);float t[3]={-1,0,0};add_tri(apex,c3,c2,n,t);}
    {float n[3];cross_norm(apex,c0,c3,n);float t[3]={0,0,1};add_tri(apex,c0,c3,n,t);}
    // Base
    {
        auto base=static_cast<uint32_t>(v.size());
        float n[3]={0,-1,0}, t[3]={1,0,0};
        float bp[4][3]={{c0[0],c0[1],c0[2]},{c3[0],c3[1],c3[2]},{c2[0],c2[1],c2[2]},{c1[0],c1[1],c1[2]}};
        for(int i=0;i<4;++i){
            TestVertex tv{};
            tv.pos[0]=bp[i][0];tv.pos[1]=bp[i][1];tv.pos[2]=bp[i][2];
            tv.normal[0]=n[0];tv.normal[1]=n[1];tv.normal[2]=n[2];
            tv.tangent[0]=t[0];tv.tangent[1]=t[1];tv.tangent[2]=t[2];tv.tangent[3]=1;
            v.push_back(tv);
        }
        idx.push_back(base);idx.push_back(base+1);idx.push_back(base+2);
        idx.push_back(base);idx.push_back(base+2);idx.push_back(base+3);
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    using namespace vivid::gpu;
    static constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

    // =====================================================================
    // CPU tests (always run, no GPU needed)
    // =====================================================================

    std::fprintf(stderr, "\n=== CPU Test: Cube geometry ===\n");
    {
        std::vector<TestVertex> verts;
        std::vector<uint32_t> indices;
        test_gen_cube(verts, indices);

        check(verts.size() == 24, "cube has 24 vertices");
        check(indices.size() == 36, "cube has 36 indices");

        // All normals should be unit length
        bool all_unit = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.normal[0]*v.normal[0] +
                                  v.normal[1]*v.normal[1] +
                                  v.normal[2]*v.normal[2]);
            if (std::fabs(len - 1.0f) > 1e-5f) { all_unit = false; break; }
        }
        check(all_unit, "all cube normals are unit length");

        // All tangents should be unit length with w=1.0
        bool tangents_ok = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.tangent[0]*v.tangent[0] +
                                  v.tangent[1]*v.tangent[1] +
                                  v.tangent[2]*v.tangent[2]);
            if (std::fabs(len - 1.0f) > 1e-5f || std::fabs(v.tangent[3] - 1.0f) > 1e-5f) {
                tangents_ok = false; break;
            }
        }
        check(tangents_ok, "all cube tangents are unit length with w=1.0");
    }

    std::fprintf(stderr, "\n=== CPU Test: Sphere geometry (detail=16) ===\n");
    {
        std::vector<TestVertex> verts;
        std::vector<uint32_t> indices;
        test_gen_sphere(verts, indices, 16);

        int stacks = 8, slices = 16;
        uint32_t expected_verts = static_cast<uint32_t>((stacks + 1) * (slices + 1));
        uint32_t expected_indices = static_cast<uint32_t>(stacks * slices * 6);

        char msg[128];
        std::snprintf(msg, sizeof(msg), "sphere has %u vertices (expected %u)",
                     static_cast<uint32_t>(verts.size()), expected_verts);
        check(verts.size() == expected_verts, msg);

        std::snprintf(msg, sizeof(msg), "sphere has %u indices (expected %u)",
                     static_cast<uint32_t>(indices.size()), expected_indices);
        check(indices.size() == expected_indices, msg);

        // All normals should be unit length
        bool all_unit = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.normal[0]*v.normal[0] +
                                  v.normal[1]*v.normal[1] +
                                  v.normal[2]*v.normal[2]);
            if (std::fabs(len - 1.0f) > 1e-4f) { all_unit = false; break; }
        }
        check(all_unit, "all sphere normals are unit length");

        // All positions should be magnitude ~0.5 (radius)
        bool all_on_sphere = true;
        for (const auto& v : verts) {
            float mag = std::sqrt(v.pos[0]*v.pos[0] + v.pos[1]*v.pos[1] + v.pos[2]*v.pos[2]);
            if (std::fabs(mag - 0.5f) > 1e-4f) { all_on_sphere = false; break; }
        }
        check(all_on_sphere, "all sphere positions at radius 0.5");

        // All tangents should be unit length with w=1.0
        bool tangents_ok = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.tangent[0]*v.tangent[0] +
                                  v.tangent[1]*v.tangent[1] +
                                  v.tangent[2]*v.tangent[2]);
            if (std::fabs(len - 1.0f) > 1e-4f || std::fabs(v.tangent[3] - 1.0f) > 1e-5f) {
                tangents_ok = false; break;
            }
        }
        check(tangents_ok, "all sphere tangents are unit length with w=1.0");
    }

    std::fprintf(stderr, "\n=== CPU Test: Cone geometry (detail=16) ===\n");
    {
        std::vector<TestVertex> verts;
        std::vector<uint32_t> indices;
        test_gen_cone(verts, indices, 16);

        // Body: 16 slices * 3 verts = 48, Cap: 1 center + 17 ring = 18 → total 66
        // Body indices: 16*3 = 48, Cap indices: 16*3 = 48 → total 96
        uint32_t expected_verts = 16 * 3 + 1 + (16 + 1);
        uint32_t expected_indices = 16 * 3 + 16 * 3;

        char msg[128];
        std::snprintf(msg, sizeof(msg), "cone has %u vertices (expected %u)",
                     static_cast<uint32_t>(verts.size()), expected_verts);
        check(verts.size() == expected_verts, msg);

        std::snprintf(msg, sizeof(msg), "cone has %u indices (expected %u)",
                     static_cast<uint32_t>(indices.size()), expected_indices);
        check(indices.size() == expected_indices, msg);

        bool all_unit = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.normal[0]*v.normal[0] +
                                  v.normal[1]*v.normal[1] +
                                  v.normal[2]*v.normal[2]);
            if (std::fabs(len - 1.0f) > 1e-4f) { all_unit = false; break; }
        }
        check(all_unit, "all cone normals are unit length");

        bool tangents_ok = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.tangent[0]*v.tangent[0] +
                                  v.tangent[1]*v.tangent[1] +
                                  v.tangent[2]*v.tangent[2]);
            if (std::fabs(len - 1.0f) > 1e-4f || std::fabs(v.tangent[3] - 1.0f) > 1e-5f) {
                tangents_ok = false; break;
            }
        }
        check(tangents_ok, "all cone tangents are unit length with w=1.0");
    }

    std::fprintf(stderr, "\n=== CPU Test: Pyramid geometry ===\n");
    {
        std::vector<TestVertex> verts;
        std::vector<uint32_t> indices;
        test_gen_pyramid(verts, indices);

        check(verts.size() == 16, "pyramid has 16 vertices");
        check(indices.size() == 18, "pyramid has 18 indices");

        bool all_unit = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.normal[0]*v.normal[0] +
                                  v.normal[1]*v.normal[1] +
                                  v.normal[2]*v.normal[2]);
            if (std::fabs(len - 1.0f) > 1e-4f) { all_unit = false; break; }
        }
        check(all_unit, "all pyramid normals are unit length");

        bool tangents_ok = true;
        for (const auto& v : verts) {
            float len = std::sqrt(v.tangent[0]*v.tangent[0] +
                                  v.tangent[1]*v.tangent[1] +
                                  v.tangent[2]*v.tangent[2]);
            if (std::fabs(len - 1.0f) > 1e-4f || std::fabs(v.tangent[3] - 1.0f) > 1e-5f) {
                tangents_ok = false; break;
            }
        }
        check(tangents_ok, "all pyramid tangents are unit length with w=1.0");
    }

    std::fprintf(stderr, "\n=== CPU Test: Transform composition ===\n");
    {
        // Test with known values: identity scale, no rotation, translation (1,2,3)
        mat4x4 S, tmp, model, T;
        mat4x4_identity(S);
        mat4x4_scale_aniso(S, S, 1.0f, 1.0f, 1.0f);
        mat4x4_rotate_X(tmp, S, 0.0f);
        mat4x4_rotate_Y(S, tmp, 0.0f);
        mat4x4_rotate_Z(tmp, S, 0.0f);
        mat4x4_translate(T, 1.0f, 2.0f, 3.0f);
        mat4x4_mul(model, T, tmp);

        // Should be a pure translation matrix
        check(std::fabs(model[3][0] - 1.0f) < 1e-5f, "translation X = 1.0");
        check(std::fabs(model[3][1] - 2.0f) < 1e-5f, "translation Y = 2.0");
        check(std::fabs(model[3][2] - 3.0f) < 1e-5f, "translation Z = 3.0");
        // Diagonal should be 1 (identity rotation + unit scale)
        check(std::fabs(model[0][0] - 1.0f) < 1e-5f, "model[0][0] = 1.0 (no scale/rotation)");
        check(std::fabs(model[1][1] - 1.0f) < 1e-5f, "model[1][1] = 1.0 (no scale/rotation)");
        check(std::fabs(model[2][2] - 1.0f) < 1e-5f, "model[2][2] = 1.0 (no scale/rotation)");

        // Test with scale (2,3,4), no rotation, no translation
        mat4x4_identity(S);
        mat4x4_scale_aniso(S, S, 2.0f, 3.0f, 4.0f);
        mat4x4_rotate_X(tmp, S, 0.0f);
        mat4x4_rotate_Y(S, tmp, 0.0f);
        mat4x4_rotate_Z(tmp, S, 0.0f);
        mat4x4_translate(T, 0.0f, 0.0f, 0.0f);
        mat4x4_mul(model, T, tmp);

        check(std::fabs(model[0][0] - 2.0f) < 1e-5f, "scale X = 2.0");
        check(std::fabs(model[1][1] - 3.0f) < 1e-5f, "scale Y = 3.0");
        check(std::fabs(model[2][2] - 4.0f) < 1e-5f, "scale Z = 4.0");
    }

    // =====================================================================
    // GPU integration tests (skip if no dylibs or no adapter)
    // =====================================================================

    std::string staging = "./.test_shape3d_staging";
    std::filesystem::create_directories(staging);

    bool has_shape3d   = std::filesystem::exists("shape3d.dylib");
    bool has_render3d  = std::filesystem::exists("render_3d.dylib");

    if (has_shape3d) {
        std::filesystem::copy_file("shape3d.dylib", staging + "/shape3d.dylib",
            std::filesystem::copy_options::overwrite_existing);
    }
    if (has_render3d) {
        std::filesystem::copy_file("render_3d.dylib", staging + "/render_3d.dylib",
            std::filesystem::copy_options::overwrite_existing);
    }

    if (!has_shape3d || !has_render3d) {
        skip("shape3d.dylib or render_3d.dylib not found — skipping GPU tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan() succeeds");
    check(registry.find("Shape3D") != nullptr, "Shape3D registered");
    check(registry.find("Render3D") != nullptr, "Render3D registered");

    std::fprintf(stderr, "\n=== GPU init ===\n");
    HeadlessGpu gpu;
    if (!gpu.init()) {
        skip("No GPU available — skipping GPU tests");
        std::filesystem::remove_all(staging);
        std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                     failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
        return failures > 0 ? 1 : 0;
    }
    check(gpu.device != nullptr, "Device created");

    // Helper to get center pixel from Render3D output
    auto get_center_pixel = [&](vivid::Scheduler& sched, uint32_t W, uint32_t H,
                                const char* render_node_id) -> std::vector<uint8_t> {
        // Find Render3D node index
        auto& nodes = sched.nodes_mut();
        for (auto& ns : nodes) {
            if (ns.node_id == render_node_id && ns.gpu_texture) {
                return readback_texture(gpu.device, gpu.queue, ns.gpu_texture, W, H);
            }
        }
        return {};
    };

    // -----------------------------------------------------------------
    // GPU Test: Shape3D(cube) → Render3D — center pixel is non-black
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D(cube) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D");   // default shape=0 (cube)
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (cube visible)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Shape3D(sphere) → Render3D — center pixel is non-black
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D(sphere) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {{"shape", 1.0f}});  // sphere
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (sphere visible)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Color param — set r=1,g=0,b=0 → red dominates
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D color param (red) ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {
            {"shape", 0.0f},  // cube
            {"r", 1.0f}, {"g", 0.0f}, {"b", 0.0f}
        });
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > gv && rv > bv, "red channel dominates (r > g and r > b)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Transform — pos_x=100 (off-screen) → center pixel is black
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D off-screen transform ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {{"pos_x", 100.0f}});
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv == 0 && gv == 0 && bv == 0,
                  "center pixel is black (shape off-screen)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Shape3D(cone) → Render3D — center pixel is non-black
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D(cone) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {{"shape", 5.0f}});  // cone
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (cone visible)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Shape3D(pyramid) → Render3D — center pixel is non-black
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D(pyramid) -> Render3D ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {{"shape", 6.0f}});  // pyramid
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(rv > 0 || gv > 0 || bv > 0, "center pixel is non-black (pyramid visible)");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Unlit — Shape3D(unlit=1, green) → Render3D
    // Verify green dominates and value matches expected unlit output
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D unlit mode (green) ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {
            {"shape", 0.0f},  // cube
            {"r", 0.0f}, {"g", 1.0f}, {"b", 0.0f},
            {"unlit", 1.0f},
        });
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         rv, gv, bv, pixels[idx+3]);
            check(gv > rv && gv > bv, "green dominates in unlit mode");
            // Unlit with emission=0: output = color * (1+0) = (0,1,0) → expect G≈255
            check(gv >= 240, "unlit green channel near 255");
        }

        sched.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Emission — Shape3D(emission=2.0) → Render3D
    // Verify brighter than default (emission=0)
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D emission ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        // Default (emission=0)
        vivid::Graph g_default;
        g_default.add_node("s1", "Shape3D", {
            {"shape", 0.0f}, {"r", 0.8f}, {"g", 0.5f}, {"b", 0.2f},
        });
        g_default.add_node("r1", "Render3D");
        g_default.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched_default;
        check(sched_default.build(g_default, registry), "build default succeeds");
        sched_default.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(sched_default, gpu, kFormat);
        auto px_default = get_center_pixel(sched_default, W, H, "r1");

        // Emission=2.0
        vivid::Graph g_emissive;
        g_emissive.add_node("s1", "Shape3D", {
            {"shape", 0.0f}, {"r", 0.8f}, {"g", 0.5f}, {"b", 0.2f},
            {"emission", 2.0f},
        });
        g_emissive.add_node("r1", "Render3D");
        g_emissive.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched_emissive;
        check(sched_emissive.build(g_emissive, registry), "build emissive succeeds");
        sched_emissive.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);
        tick_and_submit(sched_emissive, gpu, kFormat);
        auto px_emissive = get_center_pixel(sched_emissive, W, H, "r1");

        check(!px_default.empty() && !px_emissive.empty(), "both readbacks returned pixels");

        if (!px_default.empty() && !px_emissive.empty()) {
            uint32_t cx = W / 2, cy = H / 2;
            size_t idx = (cy * W + cx) * 4;
            uint8_t dr = px_default[idx], dg = px_default[idx+1], db = px_default[idx+2];
            uint8_t er = px_emissive[idx], eg = px_emissive[idx+1], eb = px_emissive[idx+2];
            std::fprintf(stderr, "  Default center: (%u, %u, %u)\n", dr, dg, db);
            std::fprintf(stderr, "  Emissive center: (%u, %u, %u)\n", er, eg, eb);
            uint32_t sum_default  = static_cast<uint32_t>(dr) + dg + db;
            uint32_t sum_emissive = static_cast<uint32_t>(er) + eg + eb;
            check(sum_emissive > sum_default, "emissive is brighter than default");
        }

        sched_default.shutdown();
        sched_emissive.shutdown();
    }

    // -----------------------------------------------------------------
    // GPU Test: Toon shading — produces visible banding (fewer unique luminances)
    // -----------------------------------------------------------------
    std::fprintf(stderr, "\n=== GPU Test: Shape3D toon shading banding ===\n");
    {
        constexpr uint32_t W = 64, H = 64;

        vivid::Graph g;
        g.add_node("s1", "Shape3D", {
            {"shape", 1.0f},  // sphere
            {"r", 1.0f}, {"g", 1.0f}, {"b", 1.0f},
            {"shading", 1.0f},       // toon
            {"toon_levels", 4.0f},
        });
        g.add_node("r1", "Render3D");
        g.add_connection("s1", "scene", "r1", "scene");

        vivid::Scheduler sched;
        check(sched.build(g, registry), "build succeeds");
        sched.allocate_gpu_textures(gpu.device, W, H, kFormat, WGPUTextureUsage_CopySrc);

        tick_and_submit(sched, gpu, kFormat);

        auto pixels = get_center_pixel(sched, W, H, "r1");
        check(!pixels.empty(), "readback returned pixels");

        if (!pixels.empty()) {
            // Center pixel should be non-black (sphere is visible)
            uint32_t cx = W / 2, cy = H / 2;
            size_t cidx = (cy * W + cx) * 4;
            uint8_t cr = pixels[cidx], cg = pixels[cidx+1], cb = pixels[cidx+2];
            std::fprintf(stderr, "  Center pixel: (%u, %u, %u, %u)\n",
                         cr, cg, cb, pixels[cidx+3]);
            check(cr > 0 || cg > 0 || cb > 0,
                  "toon sphere center is non-black");

            // Sample center column, count unique R values on non-black pixels
            std::vector<uint8_t> unique_r;
            for (uint32_t y = 0; y < H; ++y) {
                size_t idx = (y * W + cx) * 4;
                uint8_t rv = pixels[idx], gv = pixels[idx+1], bv = pixels[idx+2];
                if (rv == 0 && gv == 0 && bv == 0) continue;  // skip background
                bool found = false;
                for (uint8_t u : unique_r) {
                    if (u == rv) { found = true; break; }
                }
                if (!found) unique_r.push_back(rv);
            }
            std::fprintf(stderr, "  Unique R values in center column: %zu\n",
                         unique_r.size());
            // With 4 toon levels, expect ≤ 8 distinct values
            // (4 diffuse bands × specular on/off, plus rounding)
            check(unique_r.size() <= 8,
                  "toon shading produces ≤ 8 unique luminance bands");
        }

        sched.shutdown();
    }

    // Cleanup
    gpu.shutdown();
    std::filesystem::remove_all(staging);

    std::fprintf(stderr, "\n%s: %d failure(s), %d skipped\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures, skipped);
    return failures > 0 ? 1 : 0;
}
