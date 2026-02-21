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

std::vector<uint8_t> readbackTexturePixels(WGPUDevice device, WGPUQueue queue, WGPUTexture texture,
                                            uint32_t& outWidth, uint32_t& outHeight) {
    outWidth = 0;
    outHeight = 0;

    if (!device || !queue || !texture) return {};

    uint32_t width = wgpuTextureGetWidth(texture);
    uint32_t height = wgpuTextureGetHeight(texture);
    WGPUTextureFormat format = wgpuTextureGetFormat(texture);

    if (width == 0 || height == 0) return {};

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
            return {};
    }

    uint32_t bytesPerRow = ((width * bytesPerPixel) + 255) & ~255;
    size_t bufferSize = bytesPerRow * height;

    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = bufferSize;
    bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    bufferDesc.mappedAtCreation = false;
    WGPUBuffer readbackBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    if (!readbackBuffer) return {};

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
        return {};
    }

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
        return {};
    }

    const uint8_t* mappedData = static_cast<const uint8_t*>(
        wgpuBufferGetConstMappedRange(readbackBuffer, 0, bufferSize));
    if (!mappedData) {
        wgpuBufferUnmap(readbackBuffer);
        wgpuBufferRelease(readbackBuffer);
        return {};
    }

    // Convert to RGBA8
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

    outWidth = width;
    outHeight = height;
    return pixels;
}

FrameAnalysis analyzePixels(const uint8_t* pixels, uint32_t width, uint32_t height) {
    FrameAnalysis result;

    if (!pixels || width == 0 || height == 0) return result;

    uint32_t totalPixels = width * height;

    // Pass 1: Compute mean brightness, histogram, region brightness, color accumulation
    //         + Tier 1 extensions: 64-bin histogram, clipping, visual center, color temp, alpha
    double sumBrightness = 0.0;
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    double sumSaturation = 0.0;
    result.histogram.fill(0);
    std::array<double, 9> regionSum = {};
    std::array<int, 9> regionCount = {};
    regionSum.fill(0.0);
    regionCount.fill(0);

    std::array<double, 36> hueWeightedBins = {};
    hueWeightedBins.fill(0.0);

    std::array<int, 64> fineHistogram = {};
    fineHistogram.fill(0);

    int clipBlack = 0, clipWhite = 0;
    float minLum = 1.0f, maxLum = 0.0f;

    double sumWeightedX = 0.0, sumWeightedY = 0.0, sumWeight = 0.0;
    double sumWarmth = 0.0;
    int transparentCount = 0, opaqueCount = 0, partialCount = 0;
    double sumAlpha = 0.0;

    for (uint32_t y = 0; y < height; ++y) {
        int ry = static_cast<int>(y * 3 / height);
        if (ry > 2) ry = 2;

        float normY = static_cast<float>(y) / static_cast<float>(height);

        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = pixels + (y * width + x) * 4;
            float r = p[0] / 255.0f;
            float g = p[1] / 255.0f;
            float b = p[2] / 255.0f;
            float a = p[3] / 255.0f;

            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            sumBrightness += lum;
            sumR += r;
            sumG += g;
            sumB += b;

            int bucket = std::min(static_cast<int>(lum * 8.0f), 7);
            result.histogram[bucket]++;

            int fineBucket = std::min(static_cast<int>(lum * 64.0f), 63);
            fineHistogram[fineBucket]++;

            if (lum < 0.005f) clipBlack++;
            if (lum > 0.995f) clipWhite++;
            if (lum < minLum) minLum = lum;
            if (lum > maxLum) maxLum = lum;

            int rx = static_cast<int>(x * 3 / width);
            if (rx > 2) rx = 2;
            int region = ry * 3 + rx;
            regionSum[region] += lum;
            regionCount[region]++;

            float h, s, v;
            rgbToHSV(r, g, b, h, s, v);
            sumSaturation += s;

            float hueWeight = s * v;
            if (hueWeight > 0.05f) {
                int hueBin = static_cast<int>(h / 10.0f) % 36;
                hueWeightedBins[hueBin] += hueWeight;
            }

            float normX = static_cast<float>(x) / static_cast<float>(width);
            sumWeightedX += lum * normX;
            sumWeightedY += lum * normY;
            sumWeight += lum;

            float rgbSum = r + g + b + 1e-7f;
            sumWarmth += (r - b) / rgbSum;

            if (a < 0.01f) transparentCount++;
            else if (a > 0.99f) opaqueCount++;
            else partialCount++;
            sumAlpha += a;
        }
    }

    result.meanBrightness = static_cast<float>(sumBrightness / totalPixels);
    result.dominantColor[0] = static_cast<float>(sumR / totalPixels);
    result.dominantColor[1] = static_cast<float>(sumG / totalPixels);
    result.dominantColor[2] = static_cast<float>(sumB / totalPixels);
    result.saturationAvg = static_cast<float>(sumSaturation / totalPixels);

    for (int i = 0; i < 9; ++i) {
        result.regionBrightness[i] = (regionCount[i] > 0)
            ? static_cast<float>(regionSum[i] / regionCount[i])
            : 0.0f;
    }

    auto maxIt = std::max_element(hueWeightedBins.begin(), hueWeightedBins.end());
    if (maxIt != hueWeightedBins.end() && *maxIt > 0.0) {
        int maxBin = static_cast<int>(std::distance(hueWeightedBins.begin(), maxIt));
        result.dominantHue = maxBin * 10.0f + 5.0f;
    }

    // Texture entropy from 64-bin histogram
    {
        double entropy = 0.0;
        for (int i = 0; i < 64; ++i) {
            if (fineHistogram[i] > 0) {
                double p = static_cast<double>(fineHistogram[i]) / totalPixels;
                entropy -= p * std::log2(p);
            }
        }
        result.textureEntropy = static_cast<float>(entropy / std::log2(64.0));
    }

    result.clipBlackPct = static_cast<float>(clipBlack) / totalPixels;
    result.clipWhitePct = static_cast<float>(clipWhite) / totalPixels;
    result.headroom = 1.0f - maxLum;
    result.rangeSpan = maxLum - minLum;

    if (sumWeight > 0.0) {
        result.visualCenterX = static_cast<float>(sumWeightedX / sumWeight);
        result.visualCenterY = static_cast<float>(sumWeightedY / sumWeight);
    }

    {
        float meanWarmth = static_cast<float>(sumWarmth / totalPixels);
        result.colorTemperature = (meanWarmth + 1.0f) / 2.0f;
        result.colorTemperature = std::clamp(result.colorTemperature, 0.0f, 1.0f);
    }

    // Hue histogram: collapse 36-bin to 12-bin
    {
        double hueTotal = 0.0;
        for (int i = 0; i < 36; ++i) hueTotal += hueWeightedBins[i];

        if (hueTotal > 0.0) {
            for (int i = 0; i < 12; ++i) {
                double binSum = hueWeightedBins[i * 3] + hueWeightedBins[i * 3 + 1] + hueWeightedBins[i * 3 + 2];
                result.hueHistogram[i] = static_cast<float>(binSum / hueTotal);
            }
        }

        result.uniqueHueCount = 0;
        for (int i = 0; i < 12; ++i) {
            if (result.hueHistogram[i] > 0.05f) result.uniqueHueCount++;
        }

        double hueEntropy = 0.0;
        for (int i = 0; i < 12; ++i) {
            float p = result.hueHistogram[i];
            if (p > 0.0f) {
                hueEntropy -= p * std::log2(static_cast<double>(p));
            }
        }
        result.hueEntropy = static_cast<float>(hueEntropy / std::log2(12.0));
    }

    result.alphaOpaquePct = static_cast<float>(opaqueCount) / totalPixels;
    result.alphaTransparentPct = static_cast<float>(transparentCount) / totalPixels;
    result.alphaPartialPct = static_cast<float>(partialCount) / totalPixels;
    result.alphaMean = static_cast<float>(sumAlpha / totalPixels);

    // Pass 2: Compute contrast (standard deviation of luminance)
    double sumSqDiff = 0.0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* p = pixels + (y * width + x) * 4;
            float r = p[0] / 255.0f;
            float g = p[1] / 255.0f;
            float b = p[2] / 255.0f;
            float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            double diff = lum - result.meanBrightness;
            sumSqDiff += diff * diff;
        }
    }
    result.contrast = static_cast<float>(std::sqrt(sumSqDiff / totalPixels));

    // Pass 3: Edge density, sharpness, noise (on downsampled 512x512 buffer)
    {
        const int dsW = 512;
        const int dsH = 512;

        std::vector<float> dsLum(dsW * dsH, 0.0f);
        std::vector<int> dsCounts(dsW * dsH, 0);

        for (uint32_t y = 0; y < height; ++y) {
            int dsY = static_cast<int>(static_cast<float>(y) / height * dsH);
            if (dsY >= dsH) dsY = dsH - 1;
            for (uint32_t x = 0; x < width; ++x) {
                int dsX = static_cast<int>(static_cast<float>(x) / width * dsW);
                if (dsX >= dsW) dsX = dsW - 1;
                const uint8_t* p = pixels + (y * width + x) * 4;
                float lum = 0.2126f * (p[0] / 255.0f) + 0.7152f * (p[1] / 255.0f) + 0.0722f * (p[2] / 255.0f);
                int dsIdx = dsY * dsW + dsX;
                dsLum[dsIdx] += lum;
                dsCounts[dsIdx]++;
            }
        }
        for (int i = 0; i < dsW * dsH; ++i) {
            if (dsCounts[i] > 0) dsLum[i] /= dsCounts[i];
        }

        int edgePixels = 0;
        double sumGrad = 0.0;
        double sumLaplacian = 0.0;
        double sumLaplacianSq = 0.0;
        double sumAbsLaplacian = 0.0;
        int innerPixels = 0;
        const float edgeThreshold = 0.1f;

        for (int y = 1; y < dsH - 1; ++y) {
            for (int x = 1; x < dsW - 1; ++x) {
                float c  = dsLum[y * dsW + x];
                float l  = dsLum[y * dsW + (x - 1)];
                float r  = dsLum[y * dsW + (x + 1)];
                float t  = dsLum[(y - 1) * dsW + x];
                float bt = dsLum[(y + 1) * dsW + x];

                float gx = r - l;
                float gy = bt - t;
                float grad = std::sqrt(gx * gx + gy * gy);
                sumGrad += grad;
                if (grad > edgeThreshold) edgePixels++;

                float lap = -4.0f * c + l + r + t + bt;
                sumLaplacian += lap;
                sumLaplacianSq += lap * lap;
                sumAbsLaplacian += std::fabs(lap);

                innerPixels++;
            }
        }

        if (innerPixels > 0) {
            result.edgeDensity = static_cast<float>(edgePixels) / innerPixels;
            result.avgGradientMag = static_cast<float>(sumGrad / innerPixels);

            double meanLap = sumLaplacian / innerPixels;
            double varLap = sumLaplacianSq / innerPixels - meanLap * meanLap;
            result.sharpness = static_cast<float>(varLap);

            result.noiseLevel = static_cast<float>(sumAbsLaplacian / innerPixels);
        }
    }

    return result;
}

FrameAnalysis analyzeTexture(WGPUDevice device, WGPUQueue queue, WGPUTexture texture) {
    uint32_t width, height;
    auto pixels = readbackTexturePixels(device, queue, texture, width, height);
    if (pixels.empty()) return FrameAnalysis{};
    return analyzePixels(pixels.data(), width, height);
}

} // namespace vivid
