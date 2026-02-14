// Frame analysis implementation
// GPU readback and statistical analysis of texture content

#include <vivid/frame_analysis.h>
#include <webgpu/wgpu.h>  // For wgpuDevicePoll (wgpu-native extension)
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

namespace vivid {

// Half-float decode helper (same as video_exporter_mac.mm)
static float halfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;

    if (exp == 0) {
        if (mant == 0) return sign ? -0.0f : 0.0f;
        // Denormalized
        float val = std::ldexp(static_cast<float>(mant), -24);
        return sign ? -val : val;
    }
    if (exp == 31) {
        return mant ? std::numeric_limits<float>::quiet_NaN()
                    : (sign ? -std::numeric_limits<float>::infinity()
                            : std::numeric_limits<float>::infinity());
    }

    float val = std::ldexp(static_cast<float>(mant | 0x400), static_cast<int>(exp) - 25);
    return sign ? -val : val;
}

// sRGB gamma (linear -> sRGB)
static float linearToSRGB(float c) {
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// RGB to HSV helper
static void rgbToHSV(float r, float g, float b, float& h, float& s, float& v) {
    float maxC = std::max({r, g, b});
    float minC = std::min({r, g, b});
    float delta = maxC - minC;

    v = maxC;
    s = (maxC > 0.0f) ? (delta / maxC) : 0.0f;

    if (delta < 1e-6f) {
        h = 0.0f;
    } else if (maxC == r) {
        h = 60.0f * std::fmod((g - b) / delta, 6.0f);
    } else if (maxC == g) {
        h = 60.0f * ((b - r) / delta + 2.0f);
    } else {
        h = 60.0f * ((r - g) / delta + 4.0f);
    }
    if (h < 0.0f) h += 360.0f;
}

FrameAnalysis analyzeTexture(WGPUDevice device, WGPUQueue queue, WGPUTexture texture) {
    FrameAnalysis result;

    if (!device || !queue || !texture) return result;

    // Get texture dimensions and format
    uint32_t width = wgpuTextureGetWidth(texture);
    uint32_t height = wgpuTextureGetHeight(texture);
    WGPUTextureFormat format = wgpuTextureGetFormat(texture);

    if (width == 0 || height == 0) return result;

    // Determine bytes per pixel based on format
    uint32_t bytesPerPixel = 4;
    bool isFloat16 = false;
    bool isFloat32 = false;
    bool isBGRA = false;

    switch (format) {
        case WGPUTextureFormat_RGBA8Unorm:
        case WGPUTextureFormat_RGBA8UnormSrgb:
            bytesPerPixel = 4;
            break;
        case WGPUTextureFormat_BGRA8Unorm:
        case WGPUTextureFormat_BGRA8UnormSrgb:
            bytesPerPixel = 4;
            isBGRA = true;
            break;
        case WGPUTextureFormat_RGBA16Float:
            bytesPerPixel = 8;
            isFloat16 = true;
            break;
        case WGPUTextureFormat_RGBA32Float:
            bytesPerPixel = 16;
            isFloat32 = true;
            break;
        default:
            std::cerr << "[FrameAnalysis] Unsupported texture format: " << static_cast<int>(format) << "\n";
            return result;
    }

    // Create staging buffer with 256-byte row alignment
    uint32_t bytesPerRow = ((width * bytesPerPixel) + 255) & ~255;
    size_t bufferSize = bytesPerRow * height;

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDesc.mappedAtCreation = false;
    WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    if (!readbackBuffer) return result;

    // Copy texture to buffer
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUTexelCopyTextureInfo srcInfo = {};
    srcInfo.texture = texture;
    srcInfo.mipLevel = 0;
    srcInfo.origin = {0, 0, 0};
    srcInfo.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo dstInfo = {};
    dstInfo.buffer = readbackBuffer;
    dstInfo.layout.offset = 0;
    dstInfo.layout.bytesPerRow = bytesPerRow;
    dstInfo.layout.rowsPerImage = height;

    WGPUExtent3D copySize = {width, height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcInfo, &dstInfo, &copySize);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Wait for queue to finish
    struct WorkDoneContext { std::atomic<bool> done{false}; } workCtx;
    WGPUQueueWorkDoneCallbackInfo workDoneInfo = {};
    workDoneInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    workDoneInfo.callback = [](WGPUQueueWorkDoneStatus, void* userdata1, void*) {
        auto* ctx = static_cast<WorkDoneContext*>(userdata1);
        ctx->done = true;
    };
    workDoneInfo.userdata1 = &workCtx;
    workDoneInfo.userdata2 = nullptr;
    wgpuQueueOnSubmittedWorkDone(queue, workDoneInfo);

    for (int i = 0; i < 1000 && !workCtx.done.load(); ++i) {
        wgpuDevicePoll(device, false, nullptr);
        if (!workCtx.done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (!workCtx.done.load()) {
        wgpuBufferRelease(readbackBuffer);
        return result;
    }

    // Map the buffer
    struct MapContext {
        std::atomic<bool> done{false};
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Unknown;
    } mapCtx;
    WGPUBufferMapCallbackInfo callbackInfo = {};
    callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView,
                               void* userdata1, void*) {
        auto* ctx = static_cast<MapContext*>(userdata1);
        ctx->status = status;
        ctx->done = true;
    };
    callbackInfo.userdata1 = &mapCtx;
    callbackInfo.userdata2 = nullptr;
    wgpuBufferMapAsync(readbackBuffer, WGPUMapMode_Read, 0, bufferSize, callbackInfo);

    for (int i = 0; i < 1000 && !mapCtx.done.load(); ++i) {
        wgpuDevicePoll(device, false, nullptr);
        if (!mapCtx.done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (!mapCtx.done.load() || mapCtx.status != WGPUMapAsyncStatus_Success) {
        wgpuBufferRelease(readbackBuffer);
        return result;
    }

    const uint8_t* mappedData = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));
    if (!mappedData) {
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return result;
    }

    // Convert to RGBA8 for analysis
    std::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* srcRow = mappedData + y * bytesPerRow;
        uint8_t* dstRow = pixels.data() + y * width * 4;

        if (isFloat32) {
            const float* fp = reinterpret_cast<const float*>(srcRow);
            for (uint32_t x = 0; x < width; ++x) {
                dstRow[x * 4 + 0] = static_cast<uint8_t>(std::clamp(linearToSRGB(fp[x * 4 + 0]), 0.0f, 1.0f) * 255.0f);
                dstRow[x * 4 + 1] = static_cast<uint8_t>(std::clamp(linearToSRGB(fp[x * 4 + 1]), 0.0f, 1.0f) * 255.0f);
                dstRow[x * 4 + 2] = static_cast<uint8_t>(std::clamp(linearToSRGB(fp[x * 4 + 2]), 0.0f, 1.0f) * 255.0f);
                dstRow[x * 4 + 3] = static_cast<uint8_t>(std::clamp(fp[x * 4 + 3], 0.0f, 1.0f) * 255.0f);
            }
        } else if (isFloat16) {
            const uint16_t* hp = reinterpret_cast<const uint16_t*>(srcRow);
            for (uint32_t x = 0; x < width; ++x) {
                float r = halfToFloat(hp[x * 4 + 0]);
                float g = halfToFloat(hp[x * 4 + 1]);
                float b = halfToFloat(hp[x * 4 + 2]);
                float a = halfToFloat(hp[x * 4 + 3]);
                dstRow[x * 4 + 0] = static_cast<uint8_t>(std::clamp(linearToSRGB(r), 0.0f, 1.0f) * 255.0f);
                dstRow[x * 4 + 1] = static_cast<uint8_t>(std::clamp(linearToSRGB(g), 0.0f, 1.0f) * 255.0f);
                dstRow[x * 4 + 2] = static_cast<uint8_t>(std::clamp(linearToSRGB(b), 0.0f, 1.0f) * 255.0f);
                dstRow[x * 4 + 3] = static_cast<uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f);
            }
        } else if (isBGRA) {
            for (uint32_t x = 0; x < width; ++x) {
                dstRow[x * 4 + 0] = srcRow[x * 4 + 2]; // B -> R
                dstRow[x * 4 + 1] = srcRow[x * 4 + 1]; // G
                dstRow[x * 4 + 2] = srcRow[x * 4 + 0]; // R -> B
                dstRow[x * 4 + 3] = srcRow[x * 4 + 3]; // A
            }
        } else {
            std::memcpy(dstRow, srcRow, width * 4);
        }
    }

    wgpuBufferUnmap(readbackBuffer);
    wgpuBufferRelease(readbackBuffer);

    // Now analyze the RGBA8 pixels
    uint32_t totalPixels = width * height;

    // Pass 1: Compute mean brightness, histogram, region brightness, color accumulation
    double sumBrightness = 0.0;
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    double sumSaturation = 0.0;
    result.histogram.fill(0);
    std::array<double, 9> regionSum = {};
    std::array<int, 9> regionCount = {};
    regionSum.fill(0.0);
    regionCount.fill(0);

    // Hue histogram for dominant hue (36 bins of 10 degrees)
    std::array<double, 36> hueWeightedBins = {};
    hueWeightedBins.fill(0.0);

    for (uint32_t y = 0; y < height; ++y) {
        // Determine region row (0, 1, 2)
        int ry = static_cast<int>(y * 3 / height);
        if (ry > 2) ry = 2;

        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = pixels.data() + (y * width + x) * 4;
            float r = p[0] / 255.0f;
            float g = p[1] / 255.0f;
            float b = p[2] / 255.0f;

            // Luminance (Rec. 709)
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            sumBrightness += lum;
            sumR += r;
            sumG += g;
            sumB += b;

            // Histogram bucket (8 buckets over 0-1)
            int bucket = std::min(static_cast<int>(lum * 8.0f), 7);
            result.histogram[bucket]++;

            // Region brightness
            int rx = static_cast<int>(x * 3 / width);
            if (rx > 2) rx = 2;
            int region = ry * 3 + rx;
            regionSum[region] += lum;
            regionCount[region]++;

            // HSV for saturation and hue
            float h, s, v;
            rgbToHSV(r, g, b, h, s, v);
            sumSaturation += s;

            // Weight hue by saturation * value (ignore dark/gray pixels)
            float hueWeight = s * v;
            if (hueWeight > 0.05f) {
                int hueBin = static_cast<int>(h / 10.0f) % 36;
                hueWeightedBins[hueBin] += hueWeight;
            }
        }
    }

    result.meanBrightness = static_cast<float>(sumBrightness / totalPixels);
    result.dominantColor[0] = static_cast<float>(sumR / totalPixels);
    result.dominantColor[1] = static_cast<float>(sumG / totalPixels);
    result.dominantColor[2] = static_cast<float>(sumB / totalPixels);
    result.saturationAvg = static_cast<float>(sumSaturation / totalPixels);

    // Region brightness averages
    for (int i = 0; i < 9; ++i) {
        result.regionBrightness[i] = (regionCount[i] > 0)
            ? static_cast<float>(regionSum[i] / regionCount[i])
            : 0.0f;
    }

    // Dominant hue from weighted histogram
    auto maxIt = std::max_element(hueWeightedBins.begin(), hueWeightedBins.end());
    if (maxIt != hueWeightedBins.end() && *maxIt > 0.0) {
        int maxBin = static_cast<int>(std::distance(hueWeightedBins.begin(), maxIt));
        result.dominantHue = maxBin * 10.0f + 5.0f; // Center of bin
    }

    // Pass 2: Compute contrast (standard deviation of luminance)
    double sumSqDiff = 0.0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = pixels.data() + (y * width + x) * 4;
            float r = p[0] / 255.0f;
            float g = p[1] / 255.0f;
            float b = p[2] / 255.0f;
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            double diff = lum - result.meanBrightness;
            sumSqDiff += diff * diff;
        }
    }
    result.contrast = static_cast<float>(std::sqrt(sumSqDiff / totalPixels));

    return result;
}

} // namespace vivid
