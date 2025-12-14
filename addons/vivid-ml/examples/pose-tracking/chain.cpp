// Pose Tracking Example
// Demonstrates body pose detection using MoveNet via ONNX Runtime
// with skeleton visualization overlay
//
// Requires MoveNet ONNX model in addons/vivid-ml/assets/models/movenet/singlepose-lightning.onnx
// From PINTO_model_zoo: https://github.com/PINTO0309/PINTO_model_zoo/tree/main/115_MoveNet

#include <vivid/vivid.h>
#include <vivid/video/video.h>
#include <vivid/ml/ml.h>
#include <vivid/effects/effects.h>
#include <iostream>

using namespace vivid;
using namespace vivid::video;
using namespace vivid::ml;
using namespace vivid::effects;

// Colors for different body parts
static const glm::vec4 COLOR_FACE = {0.2f, 0.8f, 1.0f, 1.0f};      // Cyan
static const glm::vec4 COLOR_ARM_L = {1.0f, 0.4f, 0.4f, 1.0f};     // Red
static const glm::vec4 COLOR_ARM_R = {0.4f, 1.0f, 0.4f, 1.0f};     // Green
static const glm::vec4 COLOR_TORSO = {1.0f, 1.0f, 0.4f, 1.0f};     // Yellow
static const glm::vec4 COLOR_LEG_L = {1.0f, 0.6f, 0.2f, 1.0f};     // Orange
static const glm::vec4 COLOR_LEG_R = {0.6f, 0.4f, 1.0f, 1.0f};     // Purple

glm::vec4 getKeypointColor(Keypoint kp) {
    int idx = static_cast<int>(kp);
    if (idx <= 4) return COLOR_FACE;
    if (idx == 5 || idx == 7 || idx == 9) return COLOR_ARM_L;
    if (idx == 6 || idx == 8 || idx == 10) return COLOR_ARM_R;
    if (idx == 11 || idx == 13 || idx == 15) return COLOR_LEG_L;
    if (idx == 12 || idx == 14 || idx == 16) return COLOR_LEG_R;
    return COLOR_TORSO;
}

glm::vec4 getConnectionColor(Keypoint from, Keypoint to) {
    int f = static_cast<int>(from);
    int t = static_cast<int>(to);

    // Face (0-4)
    if (f <= 4 && t <= 4) return COLOR_FACE;
    // Left arm (5, 7, 9)
    if ((f == 5 || f == 7 || f == 9) && (t == 5 || t == 7 || t == 9)) return COLOR_ARM_L;
    // Right arm (6, 8, 10)
    if ((f == 6 || f == 8 || f == 10) && (t == 6 || t == 8 || t == 10)) return COLOR_ARM_R;
    // Left leg (11, 13, 15)
    if ((f == 11 || f == 13 || f == 15) && (t == 11 || t == 13 || t == 15)) return COLOR_LEG_L;
    // Right leg (12, 14, 16)
    if ((f == 12 || f == 14 || f == 16) && (t == 12 || t == 14 || t == 16)) return COLOR_LEG_R;
    // Torso (shoulders and hips)
    return COLOR_TORSO;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Webcam input (pose detection source)
    auto& webcam = chain.add<Webcam>("webcam")
        .resolution(1280, 720)
        .frameRate(30);

    // Pose detector using MoveNet SinglePose Lightning
    // From PINTO_model_zoo - float32 ONNX, expects 0-1 normalized input
    chain.add<PoseDetector>("pose")
        .input(&webcam)
        .model("addons/vivid-ml/assets/models/movenet/singlepose-lightning.onnx")
        .confidenceThreshold(0.05f);

    // Canvas overlay for skeleton visualization
    auto& canvas = chain.add<Canvas>("skeleton")
        .size(1280, 720);

    // Composite webcam and skeleton overlay
    chain.add<Composite>("output")
        .input(0, &webcam)
        .input(1, &canvas)
        .mode(BlendMode::Over);

    chain.output("output");

    std::cout << "Pose Tracking Example" << std::endl;
    std::cout << "=====================" << std::endl;
    std::cout << "Skeleton overlay shows detected body pose" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& pose = chain.get<PoseDetector>("pose");
    auto& canvas = chain.get<Canvas>("skeleton");

    // Canvas dimensions
    const float width = 1280.0f;
    const float height = 720.0f;
    const float lineWidth = 4.0f;
    const float pointRadius = 8.0f;
    const float minConfidence = 0.03f;  // Min confidence to draw

    // Clear canvas with transparent background
    canvas.clear(0, 0, 0, 0);

    if (pose.detected()) {
        // Draw skeleton lines using SKELETON_CONNECTIONS from pose_detector.h
        for (const auto& bone : ml::SKELETON_CONNECTIONS) {
            float conf1 = pose.confidence(bone.from);
            float conf2 = pose.confidence(bone.to);

            // Only draw if both keypoints have sufficient confidence
            if (conf1 >= minConfidence && conf2 >= minConfidence) {
                glm::vec2 p1 = pose.keypoint(bone.from);
                glm::vec2 p2 = pose.keypoint(bone.to);

                // Convert normalized coords to pixel coords
                float x1 = p1.x * width;
                float y1 = p1.y * height;
                float x2 = p2.x * width;
                float y2 = p2.y * height;

                // Get color based on body part
                glm::vec4 color = getConnectionColor(bone.from, bone.to);

                // Fade based on average confidence
                float avgConf = (conf1 + conf2) * 0.5f;
                color.a = std::min(1.0f, avgConf * 10.0f);  // Scale up low confidence

                canvas.line(x1, y1, x2, y2, lineWidth, color);
            }
        }

        // Draw keypoint circles
        for (int i = 0; i < static_cast<int>(Keypoint::Count); i++) {
            Keypoint kp = static_cast<Keypoint>(i);
            float conf = pose.confidence(kp);

            if (conf >= minConfidence) {
                glm::vec2 p = pose.keypoint(kp);
                float x = p.x * width;
                float y = p.y * height;

                glm::vec4 color = getKeypointColor(kp);
                color.a = std::min(1.0f, conf * 10.0f);

                // Draw filled circle with outline
                canvas.circleFilled(x, y, pointRadius, color);
                canvas.circle(x, y, pointRadius, 2.0f, {1, 1, 1, color.a * 0.8f});
            }
        }
    }
}

VIVID_CHAIN(setup, update)
