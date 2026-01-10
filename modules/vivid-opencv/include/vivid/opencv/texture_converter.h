#pragma once

/**
 * @file texture_converter.h
 * @brief GPU texture <-> OpenCV cv::Mat conversion utilities
 *
 * These utilities handle the conversion between WebGPU textures and OpenCV
 * matrices. GPU->CPU readback is expensive, so use sparingly.
 */

#include <vivid/opencv/export.h>
#include <vivid/context.h>
#include <webgpu/webgpu.h>
#include <opencv2/core.hpp>

namespace vivid::opencv {

/**
 * @brief Read a GPU texture into a cv::Mat
 *
 * This function reads back a GPU texture to CPU memory using WebGPU's async
 * buffer mapping. The texture is converted from RGBA16Float to CV_8UC4.
 *
 * @warning This is an expensive operation that stalls the GPU pipeline.
 * Use sparingly, typically only when the result has changed (check needsCook()).
 *
 * @param ctx Runtime context for GPU access
 * @param texture The GPU texture to read
 * @param width Texture width in pixels
 * @param height Texture height in pixels
 * @return cv::Mat in CV_8UC4 format (BGRA), or empty Mat on failure
 *
 * @par Example
 * @code
 * cv::Mat mat = textureToMat(ctx, inputTexture(), inputWidth, inputHeight);
 * if (!mat.empty()) {
 *     cv::cvtColor(mat, gray, cv::COLOR_BGRA2GRAY);
 *     cv::findContours(gray, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
 * }
 * @endcode
 */
VIVID_OPENCV_API cv::Mat textureToMat(Context& ctx, WGPUTexture texture, int width, int height);

/**
 * @brief Upload a cv::Mat to a GPU texture
 *
 * Uploads CPU pixel data to an existing GPU texture. The Mat should be
 * CV_8UC4 (BGRA format) and match the texture dimensions.
 *
 * @param ctx Runtime context for GPU access
 * @param mat Source cv::Mat (must be CV_8UC4)
 * @param texture Destination GPU texture
 *
 * @par Example
 * @code
 * cv::Mat output(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 255));
 * cv::drawContours(output, contours, -1, cv::Scalar(0, 255, 0, 255), 2);
 * matToTexture(ctx, output, m_output);
 * @endcode
 */
VIVID_OPENCV_API void matToTexture(Context& ctx, const cv::Mat& mat, WGPUTexture texture);

} // namespace vivid::opencv
