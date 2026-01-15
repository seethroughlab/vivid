# Lesson 3: Parameters

## Commands
- Run: `vivid .`
- Show UI: `vivid . --show-ui` (press Tab to toggle)

## Modules
- Core: Noise, Blur, HSV

## Lesson Focus
The parameter system and the Claude MCP workflow for persisting slider changes.

## Key Concepts
- **Parameters**: Values exposed by operators (scale, speed, radius, etc.)
- **Sliders**: UI controls in the visualizer for adjusting parameters
- **Pending changes**: Slider values that differ from the code
- **MCP workflow**: The loop of visual editing -> code updates

## MCP Workflow
When users adjust sliders, help them persist changes:

1. **Check for changes**: Call `get_pending_changes` MCP tool
2. **Review the diff**: Show what changed (e.g., "noise.scale: 4.0 -> 12.0")
3. **Edit the code**: Update chain.cpp with new values
4. **Confirm**: Call `clear_pending_changes`
5. **Verify**: Call `get_runtime_status` to ensure hot-reload succeeded

## Suggested Modifications

1. **Find the sweet spot** for noise detail: scale 2-20, octaves 1-8, speed 0.1-1.0

2. **Balance blur and detail**: More blur = smoother/dreamier, less = sharper

3. **Color exploration**: saturation 0 (grayscale) to 2 (vivid), hue shift 0-1

## Troubleshooting
- **Sliders not appearing**: Press Tab to open visualizer, click the operator
- **Changes not persisting**: Need to update code (manually or via Claude)
- **Hot reload error**: Check terminal, fix syntax, save again

## Next
04-images-assets: Loading and processing image files
