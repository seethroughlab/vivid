# Lesson 09: Custom Operators

This lesson introduces the concept of creating custom operators.

## Lesson Objectives

1. Understand the Operator lifecycle
2. Learn the TextureOperator base class
3. See how parameters are registered
4. Know when to create custom vs use built-in operators

## Key Concepts

- **Operator base class**: All operators inherit from `Operator` or `TextureOperator`
- **Lifecycle**: init() → process() (each frame) → cleanup()
- **Parameters**: `Param<T>` with registration makes sliders appear
- **Shaders**: Custom effects typically need WGSL shaders

## What the Code Demonstrates

This lesson is more conceptual than practical. The chain.cpp shows:
- A placeholder that uses built-in operators to achieve an effect
- Comments explaining where custom logic would go
- The pattern for organizing custom operator code

## Note on Custom Operators

Creating fully custom operators requires:
1. Header file with class definition
2. Implementation file with methods
3. WGSL shader for GPU code
4. Building into Vivid or a module

This is advanced usage - most users can achieve their goals with built-in operators.

## When to Help Create Custom Operators

If a user needs a custom operator:

1. **Check if built-ins work first**: Can they combine existing operators?
2. **Suggest the module approach**: For reusable operators
3. **Point to docs**: `docs/CREATING-OPERATORS.md` has full details
4. **Help with shaders**: WGSL shader code is often the core work

## Built-in Alternatives

Many "custom" effects can be achieved with built-ins:

| Desired Effect | Built-in Solution |
|----------------|-------------------|
| Color threshold | `Threshold` operator |
| Custom blend | `Composite` with blend modes |
| Edge detection | `Edge` operator |
| Color manipulation | `HSV`, `Brightness`, `Level` |
| Distortion | `Displace`, `Transform` |

## For Advanced Users

Point them to:
- `docs/CREATING-OPERATORS.md` - Full guide
- `modules/vivid-imgui/` - Module template
- `modules/vivid-core/include/vivid/effects/` - Built-in operator examples

## Next Lesson

10-project-organization: Structuring real projects effectively
