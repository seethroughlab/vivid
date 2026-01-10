/**
 * @file optical_flow.cpp
 * @brief Optical flow operator implementation
 */

#include <vivid/opencv/optical_flow.h>
#include <vivid/opencv/texture_converter.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <webgpu/webgpu.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <cmath>

namespace vivid::opencv {

// PIMPL - hides OpenCV types from header
struct OpticalFlow::Impl {
    cv::Mat prevGray;      // Previous frame (grayscale)
    cv::Mat flow;          // Flow field (2-channel float)
    bool hasPrevFrame = false;
};

OpticalFlow::OpticalFlow() : m_impl(std::make_unique<Impl>()) {
    registerParam(scale);
    registerParam(pyrScale);
    registerParam(levels);
    registerParam(winSize);
    registerParam(iterations);
    registerParam(polyN);
    registerParam(polySigma);
    registerParam(vizMode);
    registerParam(sensitivity);
}

OpticalFlow::~OpticalFlow() = default;

static WGPUStringView toStrView(const char* str) {
    return {str, strlen(str)};
}

void OpticalFlow::releaseCustomOutput() {
    if (m_cvOutputView) {
        wgpuTextureViewRelease(m_cvOutputView);
        m_cvOutputView = nullptr;
    }
    if (m_cvOutput) {
        wgpuTextureRelease(m_cvOutput);
        m_cvOutput = nullptr;
    }
}

void OpticalFlow::createOutputWithCopyDst(Context& ctx, int width, int height) {
    if (m_cvOutput && m_cvWidth == width && m_cvHeight == height) {
        return;
    }

    releaseCustomOutput();
    m_cvWidth = width;
    m_cvHeight = height;

    WGPUTextureDescriptor desc = {};
    desc.label = toStrView("OpticalFlow Output");
    desc.size.width = static_cast<uint32_t>(width);
    desc.size.height = static_cast<uint32_t>(height);
    desc.size.depthOrArrayLayers = 1;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;  // Use 8-bit for fast upload
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
                 WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;

    m_cvOutput = wgpuDeviceCreateTexture(ctx.device(), &desc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;

    m_cvOutputView = wgpuTextureCreateView(m_cvOutput, &viewDesc);
}

void OpticalFlow::cleanup() {
    releaseCustomOutput();
    m_impl->prevGray.release();
    m_impl->flow.release();
    m_impl->hasPrevFrame = false;
}

void OpticalFlow::init(Context& ctx) {
    matchInputResolution(0);
    int width = outputWidth();
    int height = outputHeight();
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;
    createOutputWithCopyDst(ctx, width, height);
}

void OpticalFlow::process(Context& ctx) {
    if (!needsCook()) {
        return;
    }

    Operator* inputOp = getInput(0);
    if (!inputOp) {
        didCook();
        return;
    }

    // Use zero-copy view instead of copying pixels
    auto cpuView = inputOp->cpuPixelView();
    if (!cpuView.valid()) {
        didCook();
        return;
    }

    int width = cpuView.width;
    int height = cpuView.height;

    if (width < 16 || height < 16) {
        didCook();
        return;
    }

    createOutputWithCopyDst(ctx, width, height);

    // Create cv::Mat from CPU pixels (BGRA) - zero-copy wrapper
    cv::Mat input(height, width, CV_8UC4, const_cast<uint8_t*>(cpuView.data));

    // Downsample for faster processing
    float s = std::clamp(static_cast<float>(scale), 0.1f, 1.0f);
    int procWidth = static_cast<int>(width * s);
    int procHeight = static_cast<int>(height * s);
    if (procWidth < 16) procWidth = 16;
    if (procHeight < 16) procHeight = 16;

    cv::Mat small;
    if (s < 0.99f) {
        cv::resize(input, small, cv::Size(procWidth, procHeight), 0, 0, cv::INTER_AREA);
    } else {
        small = input;
    }

    // Convert to grayscale
    cv::Mat gray;
    cv::cvtColor(small, gray, cv::COLOR_BGRA2GRAY);

    // Create output image at full resolution
    cv::Mat output(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 255));

    if (m_impl->hasPrevFrame && m_impl->prevGray.size() == gray.size()) {
        // Calculate optical flow using Farneback at reduced resolution
        cv::calcOpticalFlowFarneback(
            m_impl->prevGray, gray, m_impl->flow,
            static_cast<double>(pyrScale),
            static_cast<int>(levels),
            static_cast<int>(winSize),
            static_cast<int>(iterations),
            static_cast<int>(polyN),
            static_cast<double>(polySigma),
            0  // flags
        );

        float sens = static_cast<float>(sensitivity);
        int mode = static_cast<int>(vizMode);

        // Do all visualization at REDUCED resolution, then upsample final result
        cv::Mat flowChannels[2];
        cv::split(m_impl->flow, flowChannels);
        flowChannels[0] *= sens;
        flowChannels[1] *= sens;

        cv::Mat magnitude, angle;
        cv::cartToPolar(flowChannels[0], flowChannels[1], magnitude, angle, true);

        cv::Mat smallOutput;

        if (mode == 0) {
            // HSV color wheel visualization at reduced resolution
            cv::Mat hue, sat, val;
            angle.convertTo(hue, CV_8U, 0.5);
            sat = cv::Mat(procHeight, procWidth, CV_8U, cv::Scalar(255));
            magnitude.convertTo(val, CV_8U, 10.0);

            std::vector<cv::Mat> hsvChannels = {hue, sat, val};
            cv::Mat hsv;
            cv::merge(hsvChannels, hsv);

            cv::Mat rgb;
            cv::cvtColor(hsv, rgb, cv::COLOR_HSV2BGR);
            cv::cvtColor(rgb, smallOutput, cv::COLOR_BGR2BGRA);

        } else if (mode == 1) {
            // Arrow field overlay - draw at FULL resolution for quality
            // Use input image as background, upsample flow, draw arrows
            input.copyTo(output);  // Full-res background

            // Upsample flow to full resolution
            cv::Mat flowFull;
            cv::resize(m_impl->flow, flowFull, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
            float flowScale = 1.0f / s;  // Scale flow vectors to match full res

            // Draw arrows at regular grid spacing
            int step = 20;  // Pixel spacing between arrows
            for (int y = step / 2; y < height; y += step) {
                for (int x = step / 2; x < width; x += step) {
                    const cv::Vec2f& f = flowFull.at<cv::Vec2f>(y, x);
                    float fx = f[0] * flowScale * sens;
                    float fy = f[1] * flowScale * sens;
                    float mag = std::sqrt(fx * fx + fy * fy);

                    // Only draw if there's significant motion
                    if (mag > 1.0f) {
                        cv::Point2f start(static_cast<float>(x), static_cast<float>(y));
                        cv::Point2f end(x + fx * 2, y + fy * 2);
                        // Color based on magnitude (green to red)
                        int green = static_cast<int>(std::max(0.0f, 255.0f - mag * 5));
                        int red = static_cast<int>(std::min(255.0f, mag * 10));
                        cv::arrowedLine(output, start, end, cv::Scalar(0, green, red, 255), 2, cv::LINE_AA, 0, 0.3);
                    }
                }
            }
            // Skip the upsample step since we drew at full res
            smallOutput = cv::Mat();  // Mark as handled

        } else {
            // Magnitude only (grayscale) at reduced resolution
            cv::Mat gray8;
            magnitude.convertTo(gray8, CV_8U, 10.0);
            cv::cvtColor(gray8, smallOutput, cv::COLOR_GRAY2BGRA);
        }

        // Upsample final visualization to full resolution (skip if already at full res)
        if (!smallOutput.empty()) {
            if (s < 0.99f) {
                cv::resize(smallOutput, output, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);
            } else {
                smallOutput.copyTo(output);
            }
        }
    }

    // Store current frame for next iteration (at processing resolution)
    gray.copyTo(m_impl->prevGray);
    m_impl->hasPrevFrame = true;

    // Fast upload: convert BGRA to RGBA and write directly (no sRGB conversion)
    cv::Mat rgba;
    cv::cvtColor(output, rgba, cv::COLOR_BGRA2RGBA);

    WGPUTexelCopyTextureInfo dstInfo = {};
    dstInfo.texture = m_cvOutput;
    dstInfo.mipLevel = 0;
    dstInfo.origin = {0, 0, 0};
    dstInfo.aspect = WGPUTextureAspect_All;

    // RGBA8 = 4 bytes per pixel, align to 256 bytes
    uint32_t bytesPerRow = ((width * 4) + 255) & ~255;

    // Copy with proper row alignment if needed
    std::vector<uint8_t> alignedData;
    const uint8_t* uploadData = rgba.data;
    size_t uploadSize = rgba.total() * rgba.elemSize();

    if (static_cast<uint32_t>(rgba.step[0]) != bytesPerRow) {
        alignedData.resize(bytesPerRow * height);
        for (int y = 0; y < height; ++y) {
            memcpy(alignedData.data() + y * bytesPerRow, rgba.ptr(y), width * 4);
        }
        uploadData = alignedData.data();
        uploadSize = alignedData.size();
    }

    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = bytesPerRow;
    layout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    wgpuQueueWriteTexture(ctx.queue(), &dstInfo, uploadData, uploadSize, &layout, &writeSize);

    didCook();
}

} // namespace vivid::opencv

using OpenCVOpticalFlow = vivid::opencv::OpticalFlow;
REGISTER_OPERATOR(OpenCVOpticalFlow, "OpenCV", "Dense optical flow motion detection", true);
