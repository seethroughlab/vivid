// PoseDetector - MoveNet body tracking
//
// Detects 17 body keypoints using Google's MoveNet model.
// Supports both Lightning (fast) and Thunder (accurate) variants.
//
// Usage:
//   chain.add<PoseDetector>("pose")
//       .input(&webcam)
//       .model("assets/models/movenet_lightning.onnx");
//
//   void update(Context& ctx) {
//       auto& pose = chain.get<PoseDetector>("pose");
//       if (pose.detected()) {
//           glm::vec2 nose = pose.keypoint(Keypoint::Nose);
//           float conf = pose.confidence(Keypoint::Nose);
//       }
//   }

#pragma once

#include "onnx_model.h"
#include <glm/glm.hpp>
#include <array>

namespace vivid::ml {

/// MoveNet keypoint indices (17 points)
enum class Keypoint : int {
    Nose = 0,
    LeftEye = 1,
    RightEye = 2,
    LeftEar = 3,
    RightEar = 4,
    LeftShoulder = 5,
    RightShoulder = 6,
    LeftElbow = 7,
    RightElbow = 8,
    LeftWrist = 9,
    RightWrist = 10,
    LeftHip = 11,
    RightHip = 12,
    LeftKnee = 13,
    RightKnee = 14,
    LeftAnkle = 15,
    RightAnkle = 16,
    Count = 17
};

/// Skeleton connection for visualization
struct BoneConnection {
    Keypoint from;
    Keypoint to;
};

/// Standard MoveNet skeleton connections
static const std::array<BoneConnection, 16> SKELETON_CONNECTIONS = {{
    // Face
    {Keypoint::LeftEar, Keypoint::LeftEye},
    {Keypoint::LeftEye, Keypoint::Nose},
    {Keypoint::Nose, Keypoint::RightEye},
    {Keypoint::RightEye, Keypoint::RightEar},
    // Torso
    {Keypoint::LeftShoulder, Keypoint::RightShoulder},
    {Keypoint::LeftShoulder, Keypoint::LeftHip},
    {Keypoint::RightShoulder, Keypoint::RightHip},
    {Keypoint::LeftHip, Keypoint::RightHip},
    // Left arm
    {Keypoint::LeftShoulder, Keypoint::LeftElbow},
    {Keypoint::LeftElbow, Keypoint::LeftWrist},
    // Right arm
    {Keypoint::RightShoulder, Keypoint::RightElbow},
    {Keypoint::RightElbow, Keypoint::RightWrist},
    // Left leg
    {Keypoint::LeftHip, Keypoint::LeftKnee},
    {Keypoint::LeftKnee, Keypoint::LeftAnkle},
    // Right leg
    {Keypoint::RightHip, Keypoint::RightKnee},
    {Keypoint::RightKnee, Keypoint::RightAnkle},
}};

class PoseDetector : public ONNXModel {
public:
    PoseDetector();
    ~PoseDetector() override;

    // Configuration
    PoseDetector& input(Operator* op);
    PoseDetector& model(const std::string& path);
    PoseDetector& confidenceThreshold(float threshold);
    PoseDetector& drawSkeleton(bool draw);

    // Detection results
    bool detected() const { return m_detected; }

    /// Get keypoint position (normalized 0-1)
    glm::vec2 keypoint(Keypoint kp) const;
    glm::vec2 keypoint(int index) const;

    /// Get keypoint confidence (0-1)
    float confidence(Keypoint kp) const;
    float confidence(int index) const;

    /// Get all keypoints at once
    const std::array<glm::vec3, 17>& keypoints() const { return m_keypoints; }

    // Operator interface
    std::string name() const override { return "PoseDetector"; }

protected:
    void onModelLoaded() override;
    void prepareInputTensor(Context& ctx, Tensor& tensor) override;
    void processOutputTensor(const Tensor& tensor) override;

private:
    float m_confidenceThreshold = 0.3f;
    bool m_drawSkeleton = true;
    bool m_detected = false;

    // Keypoints: x, y, confidence for each of 17 points
    std::array<glm::vec3, 17> m_keypoints;

    // Model input size (MoveNet uses 192x192 or 256x256)
    int m_inputWidth = 192;
    int m_inputHeight = 192;
};

} // namespace vivid::ml
