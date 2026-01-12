# Lesson 03: Parameters

This lesson teaches the parameter system and the Claude MCP workflow.

## Lesson Objectives

1. Understand that operators expose parameters as sliders
2. Learn the MCP workflow: adjust → get_pending_changes → edit → clear
3. Practice real-time parameter adjustment
4. Understand how visual changes become code changes

## Key Concepts

- **Parameters**: Values exposed by operators (scale, speed, radius, etc.)
- **Sliders**: UI controls in the visualizer for adjusting parameters
- **Pending changes**: Slider values that differ from the code
- **MCP workflow**: The loop of visual editing → code updates

## The MCP Workflow (Important!)

When users adjust sliders, help them persist changes:

1. **Check for changes**: Call `get_pending_changes` MCP tool
2. **Review the diff**: Show what changed (e.g., "noise.scale: 4.0 → 12.0")
3. **Edit the code**: Update chain.cpp with new values
4. **Confirm**: Call `clear_pending_changes`
5. **Verify**: Call `get_runtime_status` to ensure hot-reload succeeded

Example response when user says "update my code":
```
Let me check what you've changed...
[Calls get_pending_changes]

I see you adjusted:
- noise.scale: 4.0 → 12.0
- blur.radius: 8.0 → 4.0

I'll update your chain.cpp with these values.
[Edits file]
[Calls clear_pending_changes]
[Calls get_runtime_status to verify]

Done! Your changes are now saved in the code.
```

## What the Code Demonstrates

- Multiple operators with various parameter types
- Parameters that interact (blur affects how noise looks)
- A chain designed for experimentation

## Suggested Experiments

1. **Find the sweet spot** for noise detail:
   - scale: 2-20 (size of patterns)
   - octaves: 1-8 (complexity)
   - speed: 0.1-1.0 (animation rate)

2. **Balance blur and detail**:
   - More blur = smoother, dreamier
   - Less blur = sharper, more defined

3. **Color exploration**:
   - Adjust saturation (0 = grayscale, 2 = vivid)
   - Adjust hue shift (0-1 cycles through colors)

## Common Issues

- **Sliders not appearing**: Press Tab to open visualizer, click the operator
- **Changes not persisting**: Need to update code (manually or via Claude)
- **Hot reload error**: Check terminal, fix syntax, save again

## Next Lesson

04-images-assets: Loading and processing image files
