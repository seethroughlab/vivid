#pragma once

/**
 * @file opencv.h
 * @brief OpenCV integration for Vivid
 *
 * This module provides computer vision operators using OpenCV:
 * - Contours: Edge detection and contour extraction
 * - OpticalFlow: Motion vector calculation (planned)
 * - ColorTrack: Color-based object tracking (planned)
 * - BlobTrack: Blob detection and tracking (planned)
 * - FaceDetect: Face detection (planned)
 *
 * @par Requirements
 * OpenCV 4.x must be installed:
 * - macOS: `brew install opencv`
 * - Ubuntu: `sudo apt install libopencv-dev`
 * - Windows: `vcpkg install opencv4`
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

// Note: texture_converter.h is internal-only (requires OpenCV headers)

namespace vivid::opencv {

// Future operators will be declared here as they are implemented:
// - ColorTrack
// - BlobTrack
// - FaceDetect

} // namespace vivid::opencv
