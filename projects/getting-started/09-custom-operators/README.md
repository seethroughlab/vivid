# Lesson 09: Custom Operators

Learn how to extend Vivid by creating your own operators.

## What You'll Learn

- The Operator lifecycle (init, process, cleanup)
- TextureOperator for image effects
- Parameter registration
- When and why to create custom operators

## Prerequisites

- Completed Lessons 01-08
- Familiarity with C++ and shaders

## The Concept

Custom operators let you extend Vivid with your own effects, generators, or analysis tools. They follow the same pattern as built-in operators.

## Operator Lifecycle

Every operator follows this lifecycle:

```
Constructor → init() → process() (each frame) → cleanup()
```

```cpp
class MyOperator : public TextureOperator {
public:
    void init(Context& ctx) override {
        // Called once - create GPU resources
    }

    void process(Context& ctx) override {
        // Called every frame - do your work
    }

    void cleanup() override {
        // Called on destruction - release resources
    }

    std::string name() const override {
        return "MyOperator";
    }
};
```

## Parameters

Expose parameters to the visualizer sliders:

```cpp
class MyEffect : public TextureOperator {
public:
    Param<float> intensity{"intensity", 1.0f, 0.0f, 2.0f};
    Param<float> threshold{"threshold", 0.5f, 0.0f, 1.0f};

    MyEffect() {
        registerParam(intensity);
        registerParam(threshold);
    }
};
```

## Operator Types

| Type | Use Case | Output |
|------|----------|--------|
| TextureOperator | Image effects, generators | Texture |
| Value operator | LFO, audio analysis | Single float |
| Geometry operator | 3D shapes | Mesh data |

## Creating a Custom Effect

For a full custom operator, you need:

1. **Header file** (.h) - Declare the class
2. **Implementation** (.cpp) - Define methods
3. **Shader** (.wgsl) - GPU code for the effect

See `docs/CREATING-OPERATORS.md` for the complete guide with shader examples.

## Example: Color Threshold

This example shows a simple threshold effect. The full implementation is in the chain.cpp file.

The key points:
1. Define a class extending `TextureOperator`
2. Register parameters in the constructor
3. Implement `process()` to run your shader
4. Set an input connection

## When to Create Custom Operators

**Create a custom operator when:**
- Built-in operators can't achieve your effect
- You need specialized GPU processing
- You want to package an effect for reuse
- You're building a module for distribution

**Use built-in operators when:**
- A combination of existing operators works
- You're prototyping quickly
- The effect is simple enough to chain together

## Building Modules

For distributing custom operators, create a Vivid module:

```
my-module/
├── module.json          # Module metadata
├── include/             # Headers
│   └── my-module/
│       └── my_effect.h
├── src/                 # Implementation
│   └── my_effect.cpp
└── shaders/             # WGSL shaders
    └── my_effect.wgsl
```

See `docs/MODULES.md` for module creation details.

## Next Steps

- **Lesson 10**: Project organization and best practices
- **Full guide**: `docs/CREATING-OPERATORS.md`
- **Example modules**: `modules/vivid-imgui/` shows addon structure
