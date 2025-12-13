#include <vivid/ml/pose_detector.h>
#include <iostream>
#include <algorithm>

namespace vivid::ml {

PoseDetector::PoseDetector() {
    // Initialize keypoints to invalid positions
    for (auto& kp : m_keypoints) {
        kp = glm::vec3(0.0f, 0.0f, 0.0f);
    }
}

PoseDetector::~PoseDetector() = default;

PoseDetector& PoseDetector::input(Operator* op) {
    ONNXModel::input(op);
    return *this;
}

PoseDetector& PoseDetector::model(const std::string& path) {
    ONNXModel::model(path);
    return *this;
}

PoseDetector& PoseDetector::confidenceThreshold(float threshold) {
    m_confidenceThreshold = std::clamp(threshold, 0.0f, 1.0f);
    return *this;
}

PoseDetector& PoseDetector::drawSkeleton(bool draw) {
    m_drawSkeleton = draw;
    return *this;
}

glm::vec2 PoseDetector::keypoint(Keypoint kp) const {
    return keypoint(static_cast<int>(kp));
}

glm::vec2 PoseDetector::keypoint(int index) const {
    if (index < 0 || index >= 17) {
        return glm::vec2(0.0f);
    }
    return glm::vec2(m_keypoints[index].x, m_keypoints[index].y);
}

float PoseDetector::confidence(Keypoint kp) const {
    return confidence(static_cast<int>(kp));
}

float PoseDetector::confidence(int index) const {
    if (index < 0 || index >= 17) {
        return 0.0f;
    }
    return m_keypoints[index].z;
}

void PoseDetector::onModelLoaded() {
    // MoveNet models expect either 192x192 (Lightning) or 256x256 (Thunder)
    // Check input shape to determine which variant
    if (!m_inputShapes.empty() && m_inputShapes[0].size() >= 4) {
        m_inputHeight = static_cast<int>(m_inputShapes[0][1]);
        m_inputWidth = static_cast<int>(m_inputShapes[0][2]);
        std::cout << "[PoseDetector] Model input size: " << m_inputWidth << "x" << m_inputHeight << std::endl;
    }
}

void PoseDetector::prepareInputTensor(Tensor& tensor, WGPUTextureView inputView) {
    // MoveNet expects input in NHWC format with values 0-255 (int32) or 0-1 (float)
    // Shape: [1, height, width, 3]

    // For now, use placeholder conversion
    // TODO: Implement proper GPU texture readback
    textureToTensor(inputView, tensor, m_inputWidth, m_inputHeight);
}

void PoseDetector::processOutputTensor(const Tensor& tensor) {
    // MoveNet output shape: [1, 1, 17, 3]
    // Each keypoint has [y, x, confidence] (note: y comes before x!)

    if (tensor.data.size() < 51) {  // 17 * 3
        m_detected = false;
        return;
    }

    int validKeypoints = 0;

    for (int i = 0; i < 17; i++) {
        // MoveNet outputs [y, x, score] for each keypoint
        float y = tensor.data[i * 3 + 0];
        float x = tensor.data[i * 3 + 1];
        float conf = tensor.data[i * 3 + 2];

        m_keypoints[i] = glm::vec3(x, y, conf);

        if (conf >= m_confidenceThreshold) {
            validKeypoints++;
        }
    }

    // Consider detected if we have enough valid keypoints (at least 5)
    m_detected = validKeypoints >= 5;

    if (m_detected) {
        // Log detection info occasionally
        static int frameCount = 0;
        if (++frameCount % 60 == 0) {
            std::cout << "[PoseDetector] Detected " << validKeypoints << "/17 keypoints" << std::endl;
        }
    }
}

} // namespace vivid::ml
