# Lesson 10: Project Organization

Best practices for structuring Vivid projects, especially when working with AI assistance.

## What You'll Learn

- Project folder structure
- Asset organization patterns
- CLAUDE.md for effective AI collaboration
- Configuration options with ChainConfig
- Tips for maintainable projects

## Prerequisites

- Completed Lessons 01-09

## Run It

```bash
./build/bin/vivid projects/getting-started/10-project-organization
```

## Project Structure

A well-organized Vivid project looks like this:

```
my-project/
├── chain.cpp           # Main chain code
├── CLAUDE.md           # AI assistance context (important!)
├── README.md           # Human documentation (optional)
└── assets/
    ├── images/         # Image files
    ├── video/          # Video files
    ├── audio/          # Audio files
    └── models/         # 3D models (GLTF)
```

## The CLAUDE.md File

**This is the most important file for AI-assisted development!**

When you use Claude Code with Vivid, Claude reads CLAUDE.md to understand:
- What the project does
- Current state and issues
- Your preferences and goals

### Recommended CLAUDE.md Structure

```markdown
# Project: [Name]

## What This Does
[1-2 sentence description of the visual effect]

## Current State
- Working on: [current task]
- Issues: [any problems]

## Goals
- [What you want to achieve]
- [Specific effects or behaviors]

## Style Preferences
- [Code style preferences]
- [Visual style preferences]

## Notes
- [Any important context]
- [Known limitations]
```

### Example CLAUDE.md

```markdown
# Project: Audio Visualizer

## What This Does
Creates a audio-reactive 3D scene with geometric shapes
that pulse and rotate to music.

## Current State
- Working on: Adding color transitions
- Issues: Bloom is too bright on high notes

## Goals
- Smooth transitions between colors
- Beat detection for flash effects
- Export to video for social media

## Style Preferences
- Prefer subtle animations over flashy
- Dark backgrounds with bright accents
- Use 1920x1080 resolution
```

## Window Configuration

Use `VIVID_CHAIN_CONFIG` for custom window settings:

```cpp
static vivid::ChainConfig config{
    .windowWidth = 1920,
    .windowHeight = 1080,
    .resizable = true
};
VIVID_CHAIN_CONFIG(setup, update, config)
```

### Configuration Options

| Option | Type | Description |
|--------|------|-------------|
| `windowWidth` | int | Initial window width |
| `windowHeight` | int | Initial window height |
| `resizable` | bool | Allow window resizing |
| `vsync` | bool | Vertical sync |
| `msaa` | int | Multisample anti-aliasing |

## Asset Organization Tips

### Images
```
assets/images/
├── backgrounds/     # Background textures
├── sprites/         # UI elements, overlays
└── textures/        # Material textures
```

### Audio
```
assets/audio/
├── music/          # Background music
├── sfx/            # Sound effects
└── samples/        # Audio samples for synthesis
```

### Video
```
assets/video/
├── clips/          # Video clips
└── loops/          # Looping background videos
```

## Tips for Maintainable Projects

1. **Keep chain.cpp focused**: One main effect per project
2. **Use meaningful names**: `bass_pulse` instead of `effect1`
3. **Comment sections**: Divide code into labeled sections
4. **Update CLAUDE.md**: Keep it current as the project evolves
5. **Version assets**: Use Git LFS for large files

## Working with Claude

### Effective Prompts

**Good**: "Add beat detection that triggers a flash effect"
**Better**: "Add beat detection using BeatDetect operator. When a beat is detected, briefly increase bloom intensity to create a flash, then decay back to normal."

### The MCP Workflow

1. Run your project
2. Adjust sliders in the visualizer
3. Ask Claude: "Update my code with these slider changes"
4. Claude reads `get_pending_changes` and updates chain.cpp
5. Hot reload applies the changes

### Getting Help

- Ask Claude to explain any operator
- Request suggestions for effects
- Get help debugging compile errors
- Ask for optimization tips

## Congratulations!

You've completed the Getting Started tutorial series! You now know how to:

1. Create and run Vivid chains
2. Chain operators into pipelines
3. Use parameters and the MCP workflow
4. Load images and assets
5. Create audio-reactive visuals
6. Process video and webcam
7. Render 3D scenes
8. Combine multiple modules
9. Understand custom operator patterns
10. Organize projects effectively

## Where to Go Next

- **Explore examples**: `modules/*/examples/` for advanced techniques
- **Read recipes**: `docs/RECIPES.md` for effect patterns
- **Create modules**: `docs/MODULES.md` for packaging your work
- **Join the community**: Share your creations!
