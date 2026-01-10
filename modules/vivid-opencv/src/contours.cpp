/**
 * @file contours.cpp
 * @brief Contour detection operator implementation
 */

#include <vivid/opencv/contours.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/opencv/texture_converter.h>  // For matToTexture (output upload only)
#include <webgpu/webgpu.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vivid::opencv {

// PIMPL implementation - hides OpenCV types from header
struct Contours::Impl {
    std::vector<std::vector<cv::Point>> contours;
    cv::Mat lastInput;
};

Contours::Contours() : m_impl(std::make_unique<Impl>()) {
    registerParam(threshold1);
    registerParam(threshold2);
    registerParam(mode);
    registerParam(lineWidth);
    registerParam(colorR);
    registerParam(colorG);
    registerParam(colorB);
    registerParam(colorA);
}

Contours::~Contours() = default;

// Helper to convert std::string to WGPUStringView
static WGPUStringView toStrView(const char* str) {
    return {str, strlen(str)};
}

void Contours::releaseCustomOutput() {
    if (m_cvOutputView) {
        wgpuTextureViewRelease(m_cvOutputView);
        m_cvOutputView = nullptr;
    }
    if (m_cvOutput) {
        wgpuTextureRelease(m_cvOutput);
        m_cvOutput = nullptr;
    }
}

void Contours::createOutputWithCopyDst(Context& ctx, int width, int height) {
    // Skip if same dimensions
    if (m_cvOutput && m_cvWidth == width && m_cvHeight == height) {
        return;
    }

    // Release existing
    releaseCustomOutput();

    m_cvWidth = width;
    m_cvHeight = height;

    // Custom output creation with COPY_DST flag for matToTexture uploads
    WGPUTextureDescriptor desc = {};
    desc.label = toStrView("Contours Output");
    desc.size.width = static_cast<uint32_t>(width);
    desc.size.height = static_cast<uint32_t>(height);
    desc.size.depthOrArrayLayers = 1;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA16Float;
    // Include COPY_DST for wgpuQueueWriteTexture
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

void Contours::cleanup() {
    releaseCustomOutput();
}

void Contours::init(Context& ctx) {
    // Try to match input resolution, or use default
    matchInputResolution(0);
    int width = outputWidth();
    int height = outputHeight();

    // Ensure we have at least a 1x1 texture (will be resized in process)
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;

    createOutputWithCopyDst(ctx, width, height);
}

void Contours::process(Context& ctx) {
    // Check if we need to process
    if (!needsCook()) {
        return;
    }

    // Get input operator
    Operator* inputOp = getInput(0);
    if (!inputOp) {
        didCook();
        return;
    }

    // Use zero-copy view for CPU pixels (no 8MB copy)
    auto cpuView = inputOp->cpuPixelView();
    if (!cpuView.valid()) {
        // Input doesn't provide CPU pixels - skip processing
        // This is by design: OpenCV operators require CPU pixel sources
        didCook();
        return;
    }

    int width = cpuView.width;
    int height = cpuView.height;

    // Skip if too small to process
    if (width < 16 || height < 16) {
        didCook();
        return;
    }

    // Create/resize output with COPY_DST flag
    createOutputWithCopyDst(ctx, width, height);

    // Create cv::Mat from CPU pixel data (BGRA format from VideoPlayer/Webcam) - zero-copy
    cv::Mat input(height, width, CV_8UC4, const_cast<uint8_t*>(cpuView.data));

    // Convert to grayscale
    cv::Mat gray;
    cv::cvtColor(input, gray, cv::COLOR_BGRA2GRAY);

    // Apply Canny edge detection
    cv::Mat edges;
    cv::Canny(gray, edges,
              static_cast<double>(threshold1),
              static_cast<double>(threshold2));

    // Find contours
    m_impl->contours.clear();
    int cvMode = cv::RETR_EXTERNAL;
    switch (static_cast<int>(mode)) {
        case 0: cvMode = cv::RETR_EXTERNAL; break;
        case 1: cvMode = cv::RETR_LIST; break;
        case 2: cvMode = cv::RETR_CCOMP; break;
        case 3: cvMode = cv::RETR_TREE; break;
    }
    cv::findContours(edges, m_impl->contours, cvMode, cv::CHAIN_APPROX_SIMPLE);

    // Create output image with transparent background
    cv::Mat output(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 0));

    // Draw contours
    // OpenCV uses BGR, but our Mat is BGRA, and color params are RGB
    cv::Scalar color(
        static_cast<int>(static_cast<float>(colorB) * 255),  // B
        static_cast<int>(static_cast<float>(colorG) * 255),  // G
        static_cast<int>(static_cast<float>(colorR) * 255),  // R
        static_cast<int>(static_cast<float>(colorA) * 255)   // A
    );

    int thickness = static_cast<int>(static_cast<float>(lineWidth));
    if (thickness < 1) thickness = 1;

    cv::drawContours(output, m_impl->contours, -1, color, thickness);

    // Upload result to GPU
    matToTexture(ctx, output, m_cvOutput);

    didCook();
}

size_t Contours::contourCount() const {
    return m_impl->contours.size();
}

} // namespace vivid::opencv

// Alias for registration macro (must be outside namespace)
using OpenCVContours = vivid::opencv::Contours;

// Register operator
REGISTER_OPERATOR(OpenCVContours, "OpenCV", "Detect and draw contours using Canny edge detection", true);
