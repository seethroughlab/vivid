#include <vivid/ml/onnx_model.h>
#include <vivid/context.h>
#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <numeric>

namespace vivid::ml {

// =============================================================================
// Tensor
// =============================================================================

size_t Tensor::size() const {
    if (shape.empty()) return 0;
    return std::accumulate(shape.begin(), shape.end(), 1LL, std::multiplies<int64_t>());
}

void Tensor::reshape(const std::vector<int64_t>& newShape) {
    size_t newSize = std::accumulate(newShape.begin(), newShape.end(), 1LL, std::multiplies<int64_t>());
    if (newSize != size()) {
        throw std::runtime_error("Tensor reshape: size mismatch");
    }
    shape = newShape;
}

// =============================================================================
// ONNXModel - ONNX Runtime internals
// =============================================================================

struct ONNXModel::OrtObjects {
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> session;
    std::unique_ptr<Ort::SessionOptions> sessionOptions;
    Ort::MemoryInfo memoryInfo{nullptr};

    OrtObjects() : memoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)) {
        env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "vivid-ml");
        sessionOptions = std::make_unique<Ort::SessionOptions>();

        // Enable optimizations
        sessionOptions->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // Use CoreML on macOS for GPU acceleration
#ifdef __APPLE__
        // Note: CoreML EP requires additional setup, skip for now
        // sessionOptions->AppendExecutionProvider_CoreML();
#endif
    }
};

// =============================================================================
// ONNXModel
// =============================================================================

ONNXModel::ONNXModel() : m_ort(std::make_unique<OrtObjects>()) {
}

ONNXModel::~ONNXModel() = default;

ONNXModel& ONNXModel::model(const std::string& path) {
    m_modelPath = path;
    return *this;
}

ONNXModel& ONNXModel::input(Operator* op) {
    m_inputOp = op;
    if (op) {
        setInput(0, op);
    }
    return *this;
}

void ONNXModel::init(Context& ctx) {
    if (m_modelPath.empty()) {
        std::cerr << "[ONNXModel] No model path specified" << std::endl;
        return;
    }

    try {
        // Load the ONNX model
#ifdef _WIN32
        std::wstring wpath(m_modelPath.begin(), m_modelPath.end());
        m_ort->session = std::make_unique<Ort::Session>(*m_ort->env, wpath.c_str(), *m_ort->sessionOptions);
#else
        m_ort->session = std::make_unique<Ort::Session>(*m_ort->env, m_modelPath.c_str(), *m_ort->sessionOptions);
#endif

        Ort::AllocatorWithDefaultOptions allocator;

        // Get input info
        size_t numInputs = m_ort->session->GetInputCount();
        m_inputNames.resize(numInputs);
        m_inputShapes.resize(numInputs);
        m_inputTensors.resize(numInputs);

        for (size_t i = 0; i < numInputs; i++) {
            auto namePtr = m_ort->session->GetInputNameAllocated(i, allocator);
            m_inputNames[i] = namePtr.get();

            auto typeInfo = m_ort->session->GetInputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            m_inputShapes[i] = tensorInfo.GetShape();

            // Handle dynamic dimensions (marked as -1)
            for (auto& dim : m_inputShapes[i]) {
                if (dim < 0) dim = 1;  // Default batch size
            }

            // Allocate input tensor
            m_inputTensors[i].shape = m_inputShapes[i];
            m_inputTensors[i].data.resize(m_inputTensors[i].size());
        }

        // Get output info
        size_t numOutputs = m_ort->session->GetOutputCount();
        m_outputNames.resize(numOutputs);
        m_outputShapes.resize(numOutputs);
        m_outputTensors.resize(numOutputs);

        for (size_t i = 0; i < numOutputs; i++) {
            auto namePtr = m_ort->session->GetOutputNameAllocated(i, allocator);
            m_outputNames[i] = namePtr.get();

            auto typeInfo = m_ort->session->GetOutputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            m_outputShapes[i] = tensorInfo.GetShape();

            // Handle dynamic dimensions
            for (auto& dim : m_outputShapes[i]) {
                if (dim < 0) dim = 1;
            }

            // Allocate output tensor
            m_outputTensors[i].shape = m_outputShapes[i];
            m_outputTensors[i].data.resize(m_outputTensors[i].size());
        }

        m_loaded = true;
        std::cout << "[ONNXModel] Loaded: " << m_modelPath << std::endl;
        std::cout << "  Inputs: " << numInputs << ", Outputs: " << numOutputs << std::endl;

        for (size_t i = 0; i < numInputs; i++) {
            std::cout << "  Input " << i << ": " << m_inputNames[i] << " [";
            for (size_t j = 0; j < m_inputShapes[i].size(); j++) {
                if (j > 0) std::cout << "x";
                std::cout << m_inputShapes[i][j];
            }
            std::cout << "]" << std::endl;
        }

        // Notify subclass
        onModelLoaded();

    } catch (const Ort::Exception& e) {
        std::cerr << "[ONNXModel] Failed to load model: " << e.what() << std::endl;
        m_loaded = false;
    }
}

void ONNXModel::process(Context& ctx) {
    if (!m_loaded || !m_inputOp) return;

    WGPUTextureView inputView = m_inputOp->outputView();
    if (!inputView) return;

    // Prepare input tensor (subclass can override)
    if (!m_inputTensors.empty()) {
        prepareInputTensor(m_inputTensors[0], inputView);
    }

    // Run inference
    runInference();

    // Process output (subclass can override)
    if (!m_outputTensors.empty()) {
        processOutputTensor(m_outputTensors[0]);
    }
}

void ONNXModel::cleanup() {
    m_ort->session.reset();
    m_loaded = false;
}

void ONNXModel::runInference() {
    if (!m_loaded) return;

    try {
        // Create input tensors
        std::vector<Ort::Value> inputTensors;
        std::vector<const char*> inputNames;

        for (size_t i = 0; i < m_inputTensors.size(); i++) {
            auto& tensor = m_inputTensors[i];
            inputTensors.push_back(Ort::Value::CreateTensor<float>(
                m_ort->memoryInfo,
                tensor.data.data(),
                tensor.data.size(),
                tensor.shape.data(),
                tensor.shape.size()
            ));
            inputNames.push_back(m_inputNames[i].c_str());
        }

        // Output names
        std::vector<const char*> outputNames;
        for (const auto& name : m_outputNames) {
            outputNames.push_back(name.c_str());
        }

        // Run inference
        auto outputTensors = m_ort->session->Run(
            Ort::RunOptions{nullptr},
            inputNames.data(),
            inputTensors.data(),
            inputTensors.size(),
            outputNames.data(),
            outputNames.size()
        );

        // Copy output data
        for (size_t i = 0; i < outputTensors.size(); i++) {
            auto& ortTensor = outputTensors[i];
            auto* data = ortTensor.GetTensorData<float>();
            auto shape = ortTensor.GetTensorTypeAndShapeInfo().GetShape();

            m_outputTensors[i].shape = shape;
            size_t size = std::accumulate(shape.begin(), shape.end(), 1LL, std::multiplies<int64_t>());
            m_outputTensors[i].data.assign(data, data + size);
        }

    } catch (const Ort::Exception& e) {
        std::cerr << "[ONNXModel] Inference error: " << e.what() << std::endl;
    }
}

void ONNXModel::textureToTensor(WGPUTextureView view, Tensor& tensor,
                                int targetWidth, int targetHeight) {
    // This is a placeholder - actual implementation needs GPU readback
    // For now, fill with zeros
    // TODO: Implement proper GPU texture readback

    size_t tensorSize = tensor.size();
    if (tensor.data.size() != tensorSize) {
        tensor.data.resize(tensorSize);
    }

    // Fill with placeholder data (gray image)
    std::fill(tensor.data.begin(), tensor.data.end(), 0.5f);
}

} // namespace vivid::ml
