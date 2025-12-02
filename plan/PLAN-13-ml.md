# PLAN-13: ML Integration

Machine learning integration via ONNX Runtime for pose detection, segmentation, style transfer, and more.

## Overview

ML-powered operators for advanced visual effects:

1. **Pose Detection** — Track human body poses (MoveNet, MediaPipe)
2. **Background Segmentation** — Separate foreground/background
3. **Style Transfer** — Apply artistic styles to video
4. **Object Detection** — Detect and track objects
5. **Face Detection** — Face landmarks and expressions

```
┌─────────────────────────────────────────────────────────────────┐
│                       ML PIPELINE                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Input              ONNX Runtime          Output                │
│  ┌──────────────┐  ┌──────────────┐      ┌──────────────┐      │
│  │ Video/Webcam │  │ Model        │      │ Keypoints    │      │
│  │ (texture)    │─▶│ Inference    │─────▶│ Masks        │      │
│  │              │  │ (CPU/GPU)    │      │ Textures     │      │
│  └──────────────┘  └──────────────┘      └──────────────┘      │
│                                                                 │
│  Model Sources                                                  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐          │
│  │ MoveNet      │  │ MediaPipe    │  │ Custom .onnx │          │
│  │ (pose)       │  │ (face/hands) │  │ (user model) │          │
│  └──────────────┘  └──────────────┘  └──────────────┘          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 12.17a: ONNX Runtime Integration

### Core Infrastructure

```cpp
class ONNXSession {
public:
    bool load(const std::string& modelPath);
    bool run(const Tensor& input, Tensor& output);

    // Model info
    std::vector<TensorInfo> getInputInfo() const;
    std::vector<TensorInfo> getOutputInfo() const;

private:
    Ort::Session session_;
    Ort::Env env_;
};

struct TensorInfo {
    std::string name;
    std::vector<int64_t> shape;
    ONNXTensorElementDataType type;
};
```

### Execution Providers

| Provider | Platform | Hardware |
|----------|----------|----------|
| CPU | All | Default fallback |
| CUDA | Linux/Windows | NVIDIA GPU |
| DirectML | Windows | Any DirectX 12 GPU |
| CoreML | macOS/iOS | Apple Neural Engine |
| Metal | macOS | Apple GPU |

### Implementation Tasks

- [ ] Add ONNX Runtime dependency (FetchContent or system)
- [ ] Create ONNXSession wrapper class
- [ ] Texture → Tensor conversion (GPU readback)
- [ ] Tensor → Texture conversion (GPU upload)
- [ ] Execution provider selection
- [ ] Model caching
- [ ] Async inference (non-blocking)

---

## Phase 12.17b: Pose Detection

### PoseDetector Operator

```cpp
class PoseDetector : public Operator {
public:
    PoseDetector& input(const std::string& node);
    PoseDetector& model(PoseModel model);  // MoveNet, BlazePose
    PoseDetector& maxPeople(int n);
    PoseDetector& confidenceThreshold(float t);

    // Outputs
    // - "keypoints"   : Array of detected poses
    // - "connections" : Line segments for skeleton
    // - "mask"        : Texture with pose overlay
};

struct Pose {
    std::array<Keypoint, 17> keypoints;  // MoveNet format
    float confidence;
};

struct Keypoint {
    glm::vec2 position;  // Normalized 0-1
    float confidence;
    KeypointType type;   // Nose, LeftEye, RightShoulder, etc.
};

enum class KeypointType {
    Nose, LeftEye, RightEye,
    LeftEar, RightEar,
    LeftShoulder, RightShoulder,
    LeftElbow, RightElbow,
    LeftWrist, RightWrist,
    LeftHip, RightHip,
    LeftKnee, RightKnee,
    LeftAnkle, RightAnkle
};
```

### Models

| Model | Input Size | Speed | Accuracy |
|-------|------------|-------|----------|
| MoveNet Lightning | 192x192 | Fast | Good |
| MoveNet Thunder | 256x256 | Medium | Better |
| BlazePose Lite | 256x256 | Fast | Good |
| BlazePose Full | 256x256 | Medium | Better |
| BlazePose Heavy | 256x256 | Slow | Best |

### Usage Example

```cpp
void update(Chain& chain, Context& ctx) {
    auto webcam = chain.op<Webcam>().name("cam");
    auto pose = chain.op<PoseDetector>()
        .input("cam")
        .model(PoseModel::MoveNetLightning)
        .name("pose");

    // Get detected poses
    auto poses = chain.get<std::vector<Pose>>("pose", "keypoints");

    // Draw skeleton on output
    auto overlay = chain.op<PoseOverlay>()
        .input("cam")
        .poses("pose", "keypoints")
        .name("out");
}
```

### Implementation Tasks

- [ ] Download MoveNet models (TFLite → ONNX conversion)
- [ ] Image preprocessing (resize, normalize)
- [ ] Postprocessing (decode keypoints)
- [ ] Multi-person detection
- [ ] Skeleton rendering
- [ ] Temporal smoothing

---

## Phase 12.17c: Background Segmentation

### SegmentBackground Operator

```cpp
class SegmentBackground : public Operator {
public:
    SegmentBackground& input(const std::string& node);
    SegmentBackground& model(SegmentModel model);
    SegmentBackground& edgeSmoothing(float amount);
    SegmentBackground& dilate(int pixels);

    // Outputs
    // - "mask"      : Grayscale segmentation mask
    // - "foreground": Input with background removed
    // - "background": Background only
};
```

### Models

| Model | Input Size | Speed | Edge Quality |
|-------|------------|-------|--------------|
| MediaPipe Selfie | 256x256 | Fast | Good |
| BodyPix | 640x480 | Medium | Better |
| MODNet | 512x512 | Slow | Best |
| Robust Video Matting | 1920x1080 | Medium | Excellent |

### Usage Example

```cpp
void update(Chain& chain, Context& ctx) {
    auto webcam = chain.op<Webcam>().name("cam");

    auto segment = chain.op<SegmentBackground>()
        .input("cam")
        .model(SegmentModel::MediaPipeSelfie)
        .name("seg");

    // Composite foreground over new background
    auto background = chain.op<Noise>().name("bg");

    auto composite = chain.op<Composite>()
        .a("seg", "foreground")
        .b("bg")
        .mask("seg", "mask")
        .name("out");
}
```

### Implementation Tasks

- [ ] Download segmentation models
- [ ] Mask postprocessing (blur, erode, dilate)
- [ ] Edge refinement
- [ ] Temporal consistency (prevent flicker)
- [ ] Hair/fine detail handling

---

## Phase 12.17d: Style Transfer

### StyleTransfer Operator

```cpp
class StyleTransfer : public Operator {
public:
    StyleTransfer& input(const std::string& node);
    StyleTransfer& style(const std::string& stylePath);  // Image or .onnx
    StyleTransfer& strength(float s);  // 0-1, blend with original
    StyleTransfer& preserveColor(bool p);

    // Output: Stylized texture
};
```

### Pre-trained Styles

| Style | Description |
|-------|-------------|
| Mosaic | Colorful geometric patterns |
| Candy | Bright, candy-like colors |
| Udnie | Abstract expressionist |
| Rain Princess | Impressionist rain effect |
| Starry Night | Van Gogh style |
| Wave | Hokusai wave style |

### Arbitrary Style Transfer

For any style image, use fast arbitrary style transfer models:

```cpp
auto style = chain.op<StyleTransfer>()
    .input("video")
    .style("assets/my_painting.jpg")  // Any image!
    .strength(0.8f)
    .name("styled");
```

### Implementation Tasks

- [ ] Pre-trained style models
- [ ] Arbitrary style transfer model
- [ ] Style blending (strength parameter)
- [ ] Color preservation mode
- [ ] Real-time optimization (lower resolution, skip frames)

---

## Phase 12.17e: Object Detection

### ObjectDetector Operator

```cpp
class ObjectDetector : public Operator {
public:
    ObjectDetector& input(const std::string& node);
    ObjectDetector& model(DetectionModel model);  // YOLO, SSD
    ObjectDetector& classes(const std::vector<std::string>& filter);
    ObjectDetector& confidenceThreshold(float t);
    ObjectDetector& nmsThreshold(float t);

    // Outputs
    // - "boxes"  : Bounding box array
    // - "labels" : Class labels
    // - "overlay": Input with boxes drawn
};

struct Detection {
    glm::vec4 bbox;      // x, y, width, height (normalized)
    std::string label;
    float confidence;
    int classId;
};
```

### Models

| Model | Speed | Classes | mAP |
|-------|-------|---------|-----|
| YOLOv8n | Fast | 80 | 37.3 |
| YOLOv8s | Medium | 80 | 44.9 |
| YOLOv8m | Slow | 80 | 50.2 |
| SSD MobileNet | Fast | 80 | 21.0 |

### Implementation Tasks

- [ ] YOLO model integration
- [ ] Non-maximum suppression
- [ ] Class filtering
- [ ] Bounding box rendering
- [ ] Object tracking (assign IDs across frames)

---

## Phase 12.17f: Face Detection & Landmarks

### FaceDetector Operator

```cpp
class FaceDetector : public Operator {
public:
    FaceDetector& input(const std::string& node);
    FaceDetector& landmarks(bool enable);  // 68/468 point landmarks
    FaceDetector& expressions(bool enable); // Emotion detection
    FaceDetector& maxFaces(int n);

    // Outputs
    // - "faces"      : Array of detected faces
    // - "landmarks"  : Facial landmark points
    // - "expressions": Emotion probabilities
    // - "mesh"       : 3D face mesh (if MediaPipe)
};

struct Face {
    glm::vec4 bbox;
    std::array<glm::vec2, 68> landmarks;  // dlib format
    float confidence;
};

struct Expression {
    float neutral, happy, sad, angry, surprised, disgusted, fearful;
};
```

### Implementation Tasks

- [ ] Face detection model (BlazeFace, dlib)
- [ ] Landmark detection (68 point or 468 point)
- [ ] 3D face mesh (MediaPipe Face Mesh)
- [ ] Expression classification
- [ ] Face tracking across frames

---

## Model Management

### Model Registry

```cpp
class ModelRegistry {
public:
    // Download and cache models
    std::filesystem::path getModelPath(const std::string& name);
    void downloadModel(const std::string& name);
    void clearCache();

    // Available models
    std::vector<ModelInfo> listModels() const;
};

struct ModelInfo {
    std::string name;
    std::string url;
    size_t sizeBytes;
    std::string description;
    bool cached;
};
```

### Model Download

Models downloaded on first use and cached in `~/.vivid/models/`:

```
~/.vivid/models/
├── movenet_lightning.onnx
├── mediapipe_selfie.onnx
├── yolov8n.onnx
└── style_mosaic.onnx
```

---

## Dependencies

| Library | Purpose | License |
|---------|---------|---------|
| ONNX Runtime | ML inference | MIT |
| stb_image | Image I/O | Public Domain |

### ONNX Runtime Configuration

```cmake
FetchContent_Declare(
    onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-osx-arm64-1.16.3.tgz
)
FetchContent_MakeAvailable(onnxruntime)

target_link_libraries(vivid-runtime PRIVATE onnxruntime)
```

---

## Implementation Order

1. **ONNX Runtime Setup** — Basic inference pipeline
2. **Pose Detection** — MoveNet integration
3. **Background Segmentation** — MediaPipe Selfie
4. **Style Transfer** — Pre-trained styles
5. **Object Detection** — YOLO integration
6. **Face Detection** — BlazeFace + landmarks
7. **Model Registry** — Download and caching

---

## Performance Considerations

1. **Resolution** — Run inference at lower resolution, upscale mask
2. **Frame Skipping** — Run inference every N frames, interpolate
3. **Async Inference** — Run on separate thread, use previous result
4. **GPU Acceleration** — Use DirectML/CoreML/CUDA when available
5. **Quantization** — Use INT8 models for faster CPU inference

---

## References

- [ONNX Runtime](https://onnxruntime.ai/)
- [MoveNet](https://tfhub.dev/google/movenet/singlepose/lightning/4)
- [MediaPipe](https://mediapipe.dev/)
- [YOLOv8](https://github.com/ultralytics/ultralytics)
- [Arbitrary Style Transfer](https://arxiv.org/abs/1705.06830)
