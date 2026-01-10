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
    desc.format = WGPUTextureFormat_RGBA16Float;
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment |
                 WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;

    m_cvOutput = wgpuDeviceCreateTexture(ctx.device(), &desc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA16Float;
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

    auto cpuData = inputOp->cpuPixels();
    if (!cpuData || !cpuData->valid()) {
        didCook();
        return;
    }

    int width = cpuData->width;
    int height = cpuData->height;

    if (width < 16 || height < 16) {
        didCook();
        return;
    }

    createOutputWithCopyDst(ctx, width, height);

    // Create cv::Mat from CPU pixels (BGRA)
    cv::Mat input(height, width, CV_8UC4, const_cast<uint8_t*>(cpuData->pixels.data()));

    // Convert to grayscale
    cv::Mat gray;
    cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);

    // Create output image
    cv::Mat output(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 255));

    if (m_impl->hasPrevFrame && m_impl->prevGray.size() == gray.size()) {
        // Calculate optical flow using Farneback
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

        // Visualize the flow
        if (mode == 0) {
            // HSV color wheel visualization
            cv::Mat hsv(height, width, CV_8UC3);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const cv::Vec2f& f = m_impl->flow.at<cv::Vec2f>(y, x);
                    float fx = f[0] * sens;
                    float fy = f[1] * sens;

                    float mag = std::sqrt(fx * fx + fy * fy);
                    float angle = std::atan2(fy, fx) * 180.0f / 3.14159f + 180.0f;

                    hsv.at<cv::Vec3b>(y, x) = cv::Vec3b(
                        static_cast<uint8_t>(angle / 2),           // H: 0-180
                        255,                                        // S: full
                        static_cast<uint8_t>(std::min(255.0f, mag * 10.0f))  // V: magnitude
                    );
                }
            }
            cv::Mat rgb;
            cv::cvtColor(hsv, rgb, cv::COLOR_HSV2BGR);
            cv::cvtColor(rgb, output, cv::COLOR_BGR2BGRA);

        } else if (mode == 1) {
            // Arrow field overlay
            input.copyTo(output);  // Copy input as base
            int step = 16;
            for (int y = step / 2; y < height; y += step) {
                for (int x = step / 2; x < width; x += step) {
                    const cv::Vec2f& f = m_impl->flow.at<cv::Vec2f>(y, x);
                    cv::Point2f start(static_cast<float>(x), static_cast<float>(y));
                    cv::Point2f end(x + f[0] * sens * 2, y + f[1] * sens * 2);
                    cv::arrowedLine(output, start, end, cv::Scalar(0, 255, 0, 255), 1, cv::LINE_AA, 0, 0.3);
                }
            }

        } else {
            // Magnitude only (grayscale)
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const cv::Vec2f& f = m_impl->flow.at<cv::Vec2f>(y, x);
                    float fx = f[0] * sens;
                    float fy = f[1] * sens;
                    float mag = std::sqrt(fx * fx + fy * fy);
                    uint8_t v = static_cast<uint8_t>(std::min(255.0f, mag * 10.0f));
                    output.at<cv::Vec4b>(y, x) = cv::Vec4b(v, v, v, 255);
                }
            }
        }
    }

    // Store current frame for next iteration
    gray.copyTo(m_impl->prevGray);
    m_impl->hasPrevFrame = true;

    // Upload to GPU
    matToTexture(ctx, output, m_cvOutput);

    didCook();
}

} // namespace vivid::opencv

using OpenCVOpticalFlow = vivid::opencv::OpticalFlow;
REGISTER_OPERATOR(OpenCVOpticalFlow, "OpenCV", "Dense optical flow motion detection", true);
