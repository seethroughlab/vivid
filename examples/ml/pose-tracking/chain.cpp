// Pose Tracking Example
// Demonstrates body pose detection using MoveNet via ONNX Runtime
//
// Requires MoveNet ONNX model in assets/models/movenet_lightning.onnx
// Download from: https://tfhub.dev/google/movenet/singlepose/lightning
// (Convert to ONNX using tf2onnx)

#include <vivid/vivid.h>
#include <vivid/video/video.h>
#include <vivid/ml/ml.h>
#include <vivid/effects/effects.h>
#include <iostream>

using namespace vivid;
using namespace vivid::video;
using namespace vivid::ml;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Webcam input (pose detection source)
    auto& webcam = chain.add<Webcam>("webcam")
        .resolution(640, 480)
        .frameRate(30);

    // Pose detector using MoveNet multipose
    // Converted from TensorFlow Hub movenet-tensorflow2-multipose-lightning-v1
    auto& pose = chain.add<PoseDetector>("pose")
        .input(&webcam)
        .model("assets/models/movenet/multipose-lightning.onnx")
        .confidenceThreshold(0.01f);  // Low threshold for this model

    // Simple color correction for visualization
    auto& colorCorrect = chain.add<HSV>("hsv")
        .input(&webcam)
        .saturation(1.2f);

    chain.output("hsv");

    std::cout << "Pose Tracking Example" << std::endl;
    std::cout << "=====================" << std::endl;
    std::cout << "Using webcam for pose detection" << std::endl;
    std::cout << std::endl;
    std::cout << "Keypoints (17 MoveNet points):" << std::endl;
    std::cout << "  0: Nose" << std::endl;
    std::cout << "  1-2: Left/Right Eye" << std::endl;
    std::cout << "  3-4: Left/Right Ear" << std::endl;
    std::cout << "  5-6: Left/Right Shoulder" << std::endl;
    std::cout << "  7-8: Left/Right Elbow" << std::endl;
    std::cout << "  9-10: Left/Right Wrist" << std::endl;
    std::cout << "  11-12: Left/Right Hip" << std::endl;
    std::cout << "  13-14: Left/Right Knee" << std::endl;
    std::cout << "  15-16: Left/Right Ankle" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& pose = chain.get<PoseDetector>("pose");

    // Log detection state periodically
    static int frameCount = 0;
    if (++frameCount % 120 == 0) {  // Every 2 seconds at 60fps
        if (pose.detected()) {
            std::cout << "Pose detected:" << std::endl;

            // Print key body points
            auto nose = pose.keypoint(Keypoint::Nose);
            auto leftWrist = pose.keypoint(Keypoint::LeftWrist);
            auto rightWrist = pose.keypoint(Keypoint::RightWrist);

            std::cout << "  Nose: (" << nose.x << ", " << nose.y
                      << ") conf: " << pose.confidence(Keypoint::Nose) << std::endl;
            std::cout << "  L.Wrist: (" << leftWrist.x << ", " << leftWrist.y
                      << ") conf: " << pose.confidence(Keypoint::LeftWrist) << std::endl;
            std::cout << "  R.Wrist: (" << rightWrist.x << ", " << rightWrist.y
                      << ") conf: " << pose.confidence(Keypoint::RightWrist) << std::endl;

            // Calculate arm spread (example derived metric)
            float armSpread = glm::distance(leftWrist, rightWrist);
            std::cout << "  Arm spread: " << armSpread << std::endl;
        } else {
            std::cout << "No pose detected" << std::endl;
        }
    }

    // Use pose data to control visual effects
    if (pose.detected()) {
        // Example: Use hand height to control hue
        auto leftWrist = pose.keypoint(Keypoint::LeftWrist);
        float hueShift = leftWrist.y;  // 0-1 based on vertical position

        auto& hsv = chain.get<HSV>("hsv");
        hsv.hueShift(hueShift);
    }
}

VIVID_CHAIN(setup, update)
