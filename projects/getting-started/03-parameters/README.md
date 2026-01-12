# Lesson 03: Parameters

Learn how to create interactive sliders and use the Claude MCP workflow for rapid iteration.

## What You'll Learn

- Built-in operator parameters and sliders
- The Claude MCP workflow for editing values
- Adjusting parameters in real-time
- How changes flow from UI back to code

## Prerequisites

- Completed Lesson 02: Operator Pipeline

## Run It

```bash
./build/bin/vivid projects/getting-started/03-parameters
```

## Walkthrough

### Built-in Parameters

Every operator has parameters that appear automatically in the visualizer (press Tab). For example, the Noise operator exposes:

- `scale` - Pattern size
- `speed` - Animation speed
- `octaves` - Detail layers

These show up as sliders you can drag!

### The Slider Workflow

1. **Press Tab** to open the chain visualizer
2. **Click an operator** to see its parameters
3. **Drag a slider** to change a value
4. **Watch the preview** update immediately

The sliders let you experiment quickly without editing code.

### The Claude MCP Workflow

When using Claude Code with Vivid, there's a powerful workflow:

1. **Adjust sliders** in the visualizer until you like the look
2. **Ask Claude** to check for pending changes:
   ```
   "Check get_pending_changes and update my code"
   ```
3. **Claude reads** the MCP `get_pending_changes` tool
4. **Claude edits** your chain.cpp with the new values
5. **Claude calls** `clear_pending_changes` to confirm
6. **Hot reload** applies the change

This lets you design visually, then have Claude persist your choices to code!

### Example Session

```
You: "I want the noise to be more detailed"
[Drag scale slider from 4.0 to 12.0]
[Drag octaves slider from 3 to 6]

You: "That looks good! Update my code with these values"
Claude: [Calls get_pending_changes, sees scale=12.0, octaves=6]
Claude: [Edits chain.cpp with new values]
Claude: [Calls clear_pending_changes]
```

## Try It

1. **Explore parameters**: Click each operator and see what sliders appear
2. **Find sweet spots**: Adjust blur radius, noise scale, saturation
3. **Practice the workflow**: Make changes, then ask Claude to update your code
4. **Undo changes**: The "Reset" button reverts to code values

## The Parameter System

Operators define parameters with ranges:
```cpp
noise.scale = 4.0f;  // This becomes a slider with default 4.0
```

The visualizer knows the valid range for each parameter and creates appropriate UI.

## MCP Tools Reference

| Tool | Purpose |
|------|---------|
| `get_pending_changes` | See slider values that differ from code |
| `clear_pending_changes` | Confirm changes were applied to code |
| `discard_pending_changes` | Revert sliders to code values |
| `get_live_params` | Get current real-time values |

## Next Steps

- **Lesson 04**: Load and process images
- **Advanced**: See `modules/vivid-core/examples/param-modulation` for animating parameters
