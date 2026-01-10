/**
 * @file opencv.cpp
 * @brief OpenCV integration module entry point
 *
 * This file exists primarily to ensure the module is properly linked.
 * Operator registrations happen in their respective .cpp files via
 * the REGISTER_OPERATOR macro.
 */

#include <vivid/opencv/opencv.h>

namespace vivid::opencv {

// Module initialization happens automatically via static operator registrations
// in contours.cpp and other operator files.

} // namespace vivid::opencv
