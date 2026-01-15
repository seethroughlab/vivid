# Lesson 9: Custom Operators

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Core: Various built-in operators

## Lesson Focus
Understanding the Operator lifecycle and when to create custom operators.

## Key Concepts
- **Operator base class**: All operators inherit from `Operator` or `TextureOperator`
- **Lifecycle**: init() -> process() (each frame) -> cleanup()
- **Parameters**: `Param<T>` with registration makes sliders appear
- **Shaders**: Custom effects typically need WGSL shaders

## When to Create Custom Operators
Creating fully custom operators requires:
1. Header file with class definition
2. Implementation file with methods
3. WGSL shader for GPU code
4. Building into Vivid or a module

This is advanced usage - most goals can be achieved with built-in operators.

## Built-in Alternatives
| Desired Effect | Built-in Solution |
|----------------|-------------------|
| Color threshold | `Threshold` operator |
| Custom blend | `Composite` with blend modes |
| Edge detection | `Edge` operator |
| Color manipulation | `HSV`, `Brightness`, `Level` |
| Distortion | `Displace`, `Transform` |

## Resources
- `docs/CREATING-OPERATORS.md` - Full guide
- `modules/vivid-imgui/` - Module template
- `modules/vivid-core/include/vivid/effects/` - Built-in operator examples

## Next
10-project-organization: Structuring real projects effectively
