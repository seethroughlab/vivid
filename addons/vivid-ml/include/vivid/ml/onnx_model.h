// ONNXModel - Generic ONNX model inference
//
// Base class for running ONNX models. Handles model loading, session
// management, and tensor I/O. Specialized operators (PoseDetector, etc.)
// inherit from this class.
//
// Usage:
//   chain.add<ONNXModel>("model")
//       .model("assets/models/my_model.onnx")
//       .input(&someTexture);
//
//   // Access output tensor
//   auto& model = chain.get<ONNXModel>("model");
//   auto output = model.outputTensor(0);

#pragma once

#include <vivid/operator.h>
#include <string>
#include <vector>
#include <memory>

// Forward declarations for ONNX Runtime
namespace Ort {
    struct Env;
    struct Session;
    struct SessionOptions;
    struct MemoryInfo;
    struct Value;
}

namespace vivid::ml {

/// ONNX tensor element types (subset we support)
enum class TensorType {
    Float32 = 0,
    UInt8 = 1,
    Int32 = 2
};

/// Tensor data wrapper for model I/O
struct Tensor {
    std::vector<float> data;       // For float32 tensors
    std::vector<uint8_t> dataU8;   // For uint8 tensors
    std::vector<int32_t> dataI32;  // For int32 tensors
    std::vector<int64_t> shape;    // e.g., {1, 3, 224, 224} for NCHW
    TensorType type = TensorType::Float32;

    /// Get total number of elements
    size_t size() const;

    /// Get value at index (float tensors only)
    float& operator[](size_t i) { return data[i]; }
    const float& operator[](size_t i) const { return data[i]; }

    /// Reshape (must have same total elements)
    void reshape(const std::vector<int64_t>& newShape);
};

class ONNXModel : public Operator {
public:
    ONNXModel();
    ~ONNXModel() override;

    // Configuration
    ONNXModel& model(const std::string& path);
    ONNXModel& input(Operator* op);

    // Model info (available after loading)
    bool isLoaded() const { return m_loaded; }
    std::string modelPath() const { return m_modelPath; }

    // Input/output info
    size_t inputCount() const { return m_inputNames.size(); }
    size_t outputCount() const { return m_outputNames.size(); }
    const std::string& inputName(size_t i) const { return m_inputNames[i]; }
    const std::string& outputName(size_t i) const { return m_outputNames[i]; }
    const std::vector<int64_t>& inputShape(size_t i) const { return m_inputShapes[i]; }
    const std::vector<int64_t>& outputShape(size_t i) const { return m_outputShapes[i]; }

    // Access output tensors (valid after process())
    const Tensor& outputTensor(size_t i = 0) const { return m_outputTensors[i]; }

    // Operator interface
    std::string name() const override { return "ONNXModel"; }
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

protected:
    // Subclass hooks
    virtual void onModelLoaded() {}
    virtual void prepareInputTensor(Context& ctx, Tensor& tensor) {}
    virtual void processOutputTensor(const Tensor& tensor) {}

    // Helper to run inference
    void runInference();

    // Input texture to tensor conversion (GPU readback + preprocessing)
    bool textureToTensor(Context& ctx, Tensor& tensor,
                         int targetWidth, int targetHeight);

    std::string m_modelPath;
    Operator* m_inputOp = nullptr;
    bool m_loaded = false;

    // GPU readback resources
    WGPUBuffer m_readbackBuffer = nullptr;
    size_t m_readbackBufferSize = 0;

    // ONNX Runtime objects (pimpl to avoid header pollution)
    struct OrtObjects;
    std::unique_ptr<OrtObjects> m_ort;

    // Model metadata
    std::vector<std::string> m_inputNames;
    std::vector<std::string> m_outputNames;
    std::vector<std::vector<int64_t>> m_inputShapes;
    std::vector<std::vector<int64_t>> m_outputShapes;

    // Tensor storage
    std::vector<Tensor> m_inputTensors;
    std::vector<Tensor> m_outputTensors;
};

} // namespace vivid::ml
