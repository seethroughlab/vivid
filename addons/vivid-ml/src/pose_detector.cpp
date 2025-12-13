#include <vivid/ml/pose_detector.h>
#include <vivid/context.h>
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
    // MoveNet models expect either:
    // - Singlepose: Fixed 192x192 (Lightning) or 256x256 (Thunder)
    // - Multipose: Dynamic size (recommended 256x256 or multiple of 32)
    if (!m_inputShapes.empty() && m_inputShapes[0].size() >= 4) {
        int64_t h = m_inputShapes[0][1];
        int64_t w = m_inputShapes[0][2];

        // If dynamic (was -1, converted to 1) or too small, use recommended size
        // Valid MoveNet sizes are 192, 256, 480, etc.
        if (h < 32 || w < 32) {
            m_inputWidth = 256;
            m_inputHeight = 256;
            std::cout << "[PoseDetector] Dynamic input size, using " << m_inputWidth << "x" << m_inputHeight << std::endl;
        } else {
            m_inputWidth = static_cast<int>(w);
            m_inputHeight = static_cast<int>(h);
            std::cout << "[PoseDetector] Model input size: " << m_inputWidth << "x" << m_inputHeight << std::endl;
        }
    }
}

void PoseDetector::prepareInputTensor(Context& ctx, Tensor& tensor) {
    // MoveNet expects input in NHWC format
    // Shape: [1, height, width, channels]
    // Values: 0-255 (uint8/int32) or 0-1 (float) depending on model variant

    // Update tensor shape for dynamic input models
    int channels = (tensor.shape.size() >= 4) ? static_cast<int>(tensor.shape[3]) : 3;
    tensor.shape = {1, m_inputHeight, m_inputWidth, channels};

    // Resize tensor data buffer to match actual dimensions
    size_t tensorSize = tensor.size();
    if (tensor.type == TensorType::UInt8 && tensor.dataU8.size() != tensorSize) {
        tensor.dataU8.resize(tensorSize);
    } else if (tensor.type == TensorType::Int32 && tensor.dataI32.size() != tensorSize) {
        tensor.dataI32.resize(tensorSize);
    } else if (tensor.data.size() != tensorSize) {
        tensor.data.resize(tensorSize);
    }

    // Use texture-to-tensor conversion (handles both CPU and GPU paths)
    bool success = textureToTensor(ctx, tensor, m_inputWidth, m_inputHeight);


    if (!success) {
        // If conversion fails, fill with gray placeholder
        if (tensor.type == TensorType::UInt8) {
            std::fill(tensor.dataU8.begin(), tensor.dataU8.end(), uint8_t(128));
        } else if (tensor.type == TensorType::Int32) {
            std::fill(tensor.dataI32.begin(), tensor.dataI32.end(), int32_t(128));
        } else {
            std::fill(tensor.data.begin(), tensor.data.end(), 0.5f);
        }
    }
}

void PoseDetector::processOutputTensor(const Tensor& tensor) {
    // Handle both singlepose and multipose output formats:
    // Singlepose: [1, 1, 17, 3] - 51 values total
    // Multipose: [1, 6, 56] - 6 detections × 56 values (51 keypoints + 5 bbox)

    m_detected = false;

    if (tensor.data.empty()) {
        return;
    }

    // Detect multipose format: shape [1, 6, 56] has size 336
    bool isMultipose = (tensor.data.size() == 336 || tensor.shape.size() == 3);

    if (isMultipose) {
        // Multipose: find the detection with highest average keypoint confidence
        // Each detection has 56 values: 17*3 keypoints + 5 bbox values
        int bestDetection = -1;
        float bestAvgConf = 0.0f;
        int bestValidCount = 0;

        int numDetections = static_cast<int>(tensor.data.size() / 56);
        for (int d = 0; d < numDetections; d++) {
            int offset = d * 56;
            float sumConf = 0.0f;
            int validCount = 0;

            for (int i = 0; i < 17; i++) {
                float conf = tensor.data[offset + i * 3 + 2];
                sumConf += conf;
                if (conf >= m_confidenceThreshold) {
                    validCount++;
                }
            }

            float avgConf = sumConf / 17.0f;
            if (validCount > bestValidCount || (validCount == bestValidCount && avgConf > bestAvgConf)) {
                bestAvgConf = avgConf;
                bestValidCount = validCount;
                bestDetection = d;
            }
        }

        if (bestDetection >= 0 && bestValidCount >= 5) {
            int offset = bestDetection * 56;

            for (int i = 0; i < 17; i++) {
                float y = tensor.data[offset + i * 3 + 0];
                float x = tensor.data[offset + i * 3 + 1];
                float conf = tensor.data[offset + i * 3 + 2];
                m_keypoints[i] = glm::vec3(x, y, conf);
            }

            m_detected = true;
        }
    } else {
        // Singlepose: [1, 1, 17, 3] format
        if (tensor.data.size() < 51) {
            return;
        }

        int validKeypoints = 0;

        for (int i = 0; i < 17; i++) {
            float y = tensor.data[i * 3 + 0];
            float x = tensor.data[i * 3 + 1];
            float conf = tensor.data[i * 3 + 2];

            m_keypoints[i] = glm::vec3(x, y, conf);

            if (conf >= m_confidenceThreshold) {
                validKeypoints++;
            }
        }

        m_detected = validKeypoints >= 5;
    }
}

} // namespace vivid::ml
