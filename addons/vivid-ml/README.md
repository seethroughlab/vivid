# vivid-ml

Machine learning inference for creative applications via ONNX Runtime.

## Installation

This addon is included with Vivid by default. Requires ONNX Runtime library.

## Operators

| Operator | Description |
|----------|-------------|
| `ONNXModel` | Generic ONNX model inference |
| `PoseDetector` | Body pose detection using MoveNet |

## Included Models

The addon includes pre-trained models in `assets/models/`:

| Model | File | Description |
|-------|------|-------------|
| MoveNet SinglePose Lightning | `movenet/singlepose-lightning.onnx` | Fast single-person pose (9MB) |
| MoveNet MultiPose Lightning | `movenet/multipose-lightning.onnx` | Multi-person pose (19MB) |

Models from [PINTO_model_zoo](https://github.com/PINTO0309/PINTO_model_zoo/tree/main/115_MoveNet).

## Examples

| Example | Description |
|---------|-------------|
| [pose-tracking](examples/pose-tracking) | Real-time body pose with skeleton overlay |

## Quick Start: Pose Detection

```cpp
#include <vivid/vivid.h>
#include <vivid/video/video.h>
#include <vivid/ml/ml.h>
#include <vivid/effects/effects.h>

using namespace vivid::video;
using namespace vivid::ml;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Webcam input
    chain.add<Webcam>("webcam")
        .resolution(1280, 720);

    // Pose detection
    chain.add<PoseDetector>("pose")
        .input("webcam")
        .model("addons/vivid-ml/assets/models/movenet/singlepose-lightning.onnx")
        .confidenceThreshold(0.3f);

    // Canvas for skeleton overlay
    chain.add<Canvas>("skeleton")
        .size(1280, 720);

    chain.add<Composite>("output")
        .input(0, "webcam")
        .input(1, "skeleton")
        .mode(BlendMode::Over);

    chain.output("output");
}

void update(Context& ctx) {
    auto& pose = ctx.chain().get<PoseDetector>("pose");
    auto& canvas = ctx.chain().get<Canvas>("skeleton");

    canvas.clear(0, 0, 0, 0);

    if (pose.detected()) {
        // Draw keypoints
        for (int i = 0; i < 17; i++) {
            auto kp = static_cast<Keypoint>(i);
            if (pose.confidence(kp) > 0.3f) {
                auto p = pose.keypoint(kp);
                canvas.circleFilled(p.x * 1280, p.y * 720, 8.0f, {1, 0, 0, 1});
            }
        }
    }
}

VIVID_CHAIN(setup, update)
```

## Keypoint Indices (MoveNet)

| Index | Keypoint |
|-------|----------|
| 0 | Nose |
| 1-2 | Left/Right Eye |
| 3-4 | Left/Right Ear |
| 5-6 | Left/Right Shoulder |
| 7-8 | Left/Right Elbow |
| 9-10 | Left/Right Wrist |
| 11-12 | Left/Right Hip |
| 13-14 | Left/Right Knee |
| 15-16 | Left/Right Ankle |

## API Reference

See [LLM-REFERENCE.md](../../docs/LLM-REFERENCE.md) for complete operator documentation.

## Dependencies

- vivid-core
- vivid-video (for Webcam input)
- ONNX Runtime

## License

MIT (addon code)
Apache 2.0 (MoveNet models)
