#pragma once

/**
 * @file opencv.h
 * @brief OpenCV integration for Vivid
 *
 * This module provides computer vision operators using opencv-mobile:
 * - Contours: Edge detection and contour extraction
 * - OpticalFlow: Dense motion vector calculation
 * - BlobTrack: Blob detection and tracking
 *
 * Note: Uses opencv-mobile which includes core, imgproc, video, features2d, photo.
 * For face detection and other DNN-based features, use vivid-onnx instead.
 *
 * @par Requirements
 * opencv-mobile is automatically fetched during build - no manual installation needed.
 *
 * @par Example
 * @code
 * #include <vivid/vivid.h>
 * #include <vivid/opencv/opencv.h>
 *
 * void setup(Context& ctx) {
 *     auto& chain = ctx.chain();
 *
 *     auto& img = chain.add<Image>("img");
 *     img.file = "photo.jpg";
 *
 *     auto& contours = chain.add<vivid::opencv::Contours>("contours");
 *     contours.input("img");
 *     contours.threshold1 = 50.0f;
 *     contours.threshold2 = 150.0f;
 *
 *     chain.output("contours");
 * }
 * @endcode
 */

#include <vivid/opencv/export.h>
#include <vivid/opencv/contours.h>
#include <vivid/opencv/optical_flow.h>
#include <vivid/opencv/blob_track.h>

// Note: texture_converter.h is internal-only (requires OpenCV headers)

namespace vivid::opencv {

// Note: Face detection requires the objdetect module which is not in opencv-mobile.
// Use vivid-onnx for deep learning-based face detection instead.

} // namespace vivid::opencv
