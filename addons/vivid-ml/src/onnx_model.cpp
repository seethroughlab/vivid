#include <vivid/ml/onnx_model.h>
#include <vivid/context.h>
#include <vivid/video/webcam.h>  // For CPU pixel access
#include <webgpu/wgpu.h>  // wgpu-native extensions (wgpuDevicePoll)
#include <onnxruntime_cxx_api.h>
#include <iostream>
#include <numeric>
#include <thread>
#include <chrono>
#include <atomic>
#include <cmath>

using vivid::video::Webcam;

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

ONNXModel::~ONNXModel() {
    if (m_readbackBuffer) {
        wgpuBufferRelease(m_readbackBuffer);
        m_readbackBuffer = nullptr;
    }
}

ONNXModel& ONNXModel::model(const std::string& path) {
    m_modelPath = path;
    return *this;
}

ONNXModel& ONNXModel::input(Operator* op) {
    m_inputOp = op;
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

            // Get element type
            ONNXTensorElementDataType elemType = tensorInfo.GetElementType();
            TensorType tensorType = TensorType::Float32;
            const char* typeStr = "float32";
            if (elemType == ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8) {
                tensorType = TensorType::UInt8;
                typeStr = "uint8";
            } else if (elemType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32) {
                tensorType = TensorType::Int32;
                typeStr = "int32";
            }

            // Handle dynamic dimensions (marked as -1)
            for (auto& dim : m_inputShapes[i]) {
                if (dim < 0) dim = 1;  // Default batch size
            }

            // Allocate input tensor with correct type
            m_inputTensors[i].shape = m_inputShapes[i];
            m_inputTensors[i].type = tensorType;
            size_t tensorSize = m_inputTensors[i].size();
            if (tensorType == TensorType::UInt8) {
                m_inputTensors[i].dataU8.resize(tensorSize);
            } else if (tensorType == TensorType::Int32) {
                m_inputTensors[i].dataI32.resize(tensorSize);
            } else {
                m_inputTensors[i].data.resize(tensorSize);
            }

            std::cout << "  Input " << i << ": " << m_inputNames[i] << " (" << typeStr << ") [";
            for (size_t j = 0; j < m_inputShapes[i].size(); j++) {
                if (j > 0) std::cout << "x";
                std::cout << m_inputShapes[i][j];
            }
            std::cout << "]" << std::endl;
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
        prepareInputTensor(ctx, m_inputTensors[0]);
    }

    // Run inference
    runInference();

    // Process output (subclass can override)
    if (!m_outputTensors.empty()) {
        processOutputTensor(m_outputTensors[0]);
    }
}

void ONNXModel::cleanup() {
    if (m_readbackBuffer) {
        wgpuBufferRelease(m_readbackBuffer);
        m_readbackBuffer = nullptr;
    }
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

            if (tensor.type == TensorType::UInt8) {
                inputTensors.push_back(Ort::Value::CreateTensor<uint8_t>(
                    m_ort->memoryInfo,
                    tensor.dataU8.data(),
                    tensor.dataU8.size(),
                    tensor.shape.data(),
                    tensor.shape.size()
                ));
            } else if (tensor.type == TensorType::Int32) {
                inputTensors.push_back(Ort::Value::CreateTensor<int32_t>(
                    m_ort->memoryInfo,
                    tensor.dataI32.data(),
                    tensor.dataI32.size(),
                    tensor.shape.data(),
                    tensor.shape.size()
                ));
            } else {
                inputTensors.push_back(Ort::Value::CreateTensor<float>(
                    m_ort->memoryInfo,
                    tensor.data.data(),
                    tensor.data.size(),
                    tensor.shape.data(),
                    tensor.shape.size()
                ));
            }
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

bool ONNXModel::textureToTensor(Context& ctx, Tensor& tensor,
                                 int targetWidth, int targetHeight) {
    if (!m_inputOp) return false;

    // Try to get CPU pixel data directly (more efficient than GPU readback)
    const uint8_t* pixelData = nullptr;
    uint32_t srcWidth = 0, srcHeight = 0;
    uint32_t bytesPerRow = 0;
    bool isBGRA = false;

    // Check if input is a Webcam with CPU pixel access
    if (auto* webcam = dynamic_cast<Webcam*>(m_inputOp)) {
        pixelData = webcam->cpuPixelData();
        if (pixelData && webcam->cpuPixelDataSize() > 0) {
            srcWidth = static_cast<uint32_t>(webcam->captureWidth());
            srcHeight = static_cast<uint32_t>(webcam->captureHeight());
            bytesPerRow = srcWidth * 4;  // CPU buffer is packed RGBA
            isBGRA = false;  // Webcam outputs RGBA after conversion
        }
    }

    // If no CPU pixel data available, fall back to GPU readback
    if (!pixelData) {
        WGPUTexture srcTexture = m_inputOp->outputTexture();
        if (!srcTexture) return false;

        WGPUDevice device = ctx.device();
        WGPUQueue queue = ctx.queue();

        srcWidth = wgpuTextureGetWidth(srcTexture);
        srcHeight = wgpuTextureGetHeight(srcTexture);
        WGPUTextureFormat srcFormat = wgpuTextureGetFormat(srcTexture);

        bytesPerRow = (srcWidth * 4 + 255) & ~255;  // 256-byte aligned for GPU
        size_t requiredSize = bytesPerRow * srcHeight;

        // Create or resize readback buffer
        if (!m_readbackBuffer || m_readbackBufferSize < requiredSize) {
            if (m_readbackBuffer) {
                wgpuBufferRelease(m_readbackBuffer);
            }
            WGPUBufferDescriptor bufferDesc = {};
            bufferDesc.size = requiredSize;
            bufferDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            bufferDesc.mappedAtCreation = false;
            m_readbackBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
            m_readbackBufferSize = requiredSize;
        }

        // Copy texture to buffer
        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

        WGPUTexelCopyTextureInfo srcCopy = {};
        srcCopy.texture = srcTexture;
        srcCopy.mipLevel = 0;
        srcCopy.origin = {0, 0, 0};
        srcCopy.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo dstCopy = {};
        dstCopy.buffer = m_readbackBuffer;
        dstCopy.layout.offset = 0;
        dstCopy.layout.bytesPerRow = bytesPerRow;
        dstCopy.layout.rowsPerImage = srcHeight;

        WGPUExtent3D copySize = {srcWidth, srcHeight, 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &srcCopy, &dstCopy, &copySize);

        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);

        // Wait for queue and map buffer
        struct WorkDoneContext { std::atomic<bool> done{false}; } workCtx;
        WGPUQueueWorkDoneCallbackInfo workDoneInfo = {};
        workDoneInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        workDoneInfo.callback = [](WGPUQueueWorkDoneStatus, void* ud1, void*) {
            static_cast<WorkDoneContext*>(ud1)->done = true;
        };
        workDoneInfo.userdata1 = &workCtx;
        wgpuQueueOnSubmittedWorkDone(queue, workDoneInfo);

        int workTimeout = 100;
        while (!workCtx.done && workTimeout-- > 0) {
            wgpuDevicePoll(device, false, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!workCtx.done) return false;

        struct MapContext { std::atomic<bool> done{false}; WGPUMapAsyncStatus status; } mapCtx;
        WGPUBufferMapCallbackInfo callbackInfo = {};
        callbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        callbackInfo.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* ud1, void*) {
            auto* ctx = static_cast<MapContext*>(ud1);
            ctx->status = status;
            ctx->done = true;
        };
        callbackInfo.userdata1 = &mapCtx;
        wgpuBufferMapAsync(m_readbackBuffer, WGPUMapMode_Read, 0, requiredSize, callbackInfo);

        int mapTimeout = 100;
        while (!mapCtx.done && mapTimeout-- > 0) {
            wgpuDevicePoll(device, false, nullptr);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!mapCtx.done || mapCtx.status != WGPUMapAsyncStatus_Success) return false;

        pixelData = static_cast<const uint8_t*>(
            wgpuBufferGetConstMappedRange(m_readbackBuffer, 0, requiredSize));
        if (!pixelData) {
            wgpuBufferUnmap(m_readbackBuffer);
            return false;
        }

        isBGRA = (srcFormat == WGPUTextureFormat_BGRA8Unorm ||
                  srcFormat == WGPUTextureFormat_BGRA8UnormSrgb);
    }

    // Determine tensor format from shape (NHWC vs NCHW)
    bool isNHWC = true;
    int channels = 3;
    if (tensor.shape.size() >= 4) {
        if (tensor.shape[1] <= 4) {
            isNHWC = false;
            channels = static_cast<int>(tensor.shape[1]);
        } else {
            channels = static_cast<int>(tensor.shape[3]);
        }
    }

    // Resize and convert to tensor using bilinear interpolation
    float scaleX = static_cast<float>(srcWidth) / targetWidth;
    float scaleY = static_cast<float>(srcHeight) / targetHeight;

    for (int y = 0; y < targetHeight; y++) {
        for (int x = 0; x < targetWidth; x++) {
            float srcX = (x + 0.5f) * scaleX - 0.5f;
            float srcY = (y + 0.5f) * scaleY - 0.5f;

            int x0 = std::max(0, std::min(static_cast<int>(srcX), static_cast<int>(srcWidth) - 1));
            int y0 = std::max(0, std::min(static_cast<int>(srcY), static_cast<int>(srcHeight) - 1));
            int x1 = std::max(0, std::min(x0 + 1, static_cast<int>(srcWidth) - 1));
            int y1 = std::max(0, std::min(y0 + 1, static_cast<int>(srcHeight) - 1));

            float fx = srcX - std::floor(srcX);
            float fy = srcY - std::floor(srcY);

            auto getPixel = [&](int px, int py) -> std::array<float, 4> {
                const uint8_t* p = pixelData + py * bytesPerRow + px * 4;
                return {p[isBGRA ? 2 : 0] / 255.0f, p[1] / 255.0f,
                        p[isBGRA ? 0 : 2] / 255.0f, p[3] / 255.0f};
            };

            auto p00 = getPixel(x0, y0), p10 = getPixel(x1, y0);
            auto p01 = getPixel(x0, y1), p11 = getPixel(x1, y1);

            std::array<float, 4> result;
            for (int c = 0; c < 4; c++) {
                float v0 = p00[c] * (1 - fx) + p10[c] * fx;
                float v1 = p01[c] * (1 - fx) + p11[c] * fx;
                result[c] = v0 * (1 - fy) + v1 * fy;
            }

            if (tensor.type == TensorType::UInt8) {
                // Output uint8 values (0-255)
                if (isNHWC) {
                    size_t baseIdx = (y * targetWidth + x) * channels;
                    for (int c = 0; c < channels && c < 4; c++)
                        tensor.dataU8[baseIdx + c] = static_cast<uint8_t>(result[c] * 255.0f);
                } else {
                    size_t pixelIdx = y * targetWidth + x;
                    for (int c = 0; c < channels && c < 4; c++)
                        tensor.dataU8[c * targetWidth * targetHeight + pixelIdx] = static_cast<uint8_t>(result[c] * 255.0f);
                }
            } else if (tensor.type == TensorType::Int32) {
                // Output int32 values (0-255)
                if (isNHWC) {
                    size_t baseIdx = (y * targetWidth + x) * channels;
                    for (int c = 0; c < channels && c < 4; c++)
                        tensor.dataI32[baseIdx + c] = static_cast<int32_t>(result[c] * 255.0f);
                } else {
                    size_t pixelIdx = y * targetWidth + x;
                    for (int c = 0; c < channels && c < 4; c++)
                        tensor.dataI32[c * targetWidth * targetHeight + pixelIdx] = static_cast<int32_t>(result[c] * 255.0f);
                }
            } else {
                // Output float values (0-1 range - standard for many ONNX models)
                if (isNHWC) {
                    size_t baseIdx = (y * targetWidth + x) * channels;
                    for (int c = 0; c < channels && c < 4; c++)
                        tensor.data[baseIdx + c] = result[c];  // Already 0-1
                } else {
                    size_t pixelIdx = y * targetWidth + x;
                    for (int c = 0; c < channels && c < 4; c++)
                        tensor.data[c * targetWidth * targetHeight + pixelIdx] = result[c];
                }
            }
        }
    }

    // Unmap GPU buffer if we used it
    if (m_readbackBuffer && !dynamic_cast<Webcam*>(m_inputOp)) {
        wgpuBufferUnmap(m_readbackBuffer);
    }

    return true;
}

} // namespace vivid::ml
