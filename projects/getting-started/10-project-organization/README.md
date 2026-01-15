# Lesson 10: Project Organization

Best practices for structuring Vivid projects, especially when working with AI assistance.

## What You'll Learn

- Project folder structure
- Asset organization patterns
- AGENTS.md + BRIEF.md for effective AI collaboration
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
├── AGENTS.md           # Operational context for AI (commands, conventions)
├── BRIEF.md            # Creative vision (what you want to build)
├── README.md           # Human documentation (optional)
└── assets/
    ├── images/         # Image files
    ├── video/          # Video files
    ├── audio/          # Audio files
    └── models/         # 3D models (GLTF)
```

## AI Documentation Files

Vivid uses two files for AI-assisted development, following industry best practices:

### AGENTS.md (Operational Context)

Keep this **under 60 lines**. Contains:
- Commands (how to run/build)
- Modules used
- MCP workflow
- Code conventions
- Boundaries (what not to modify)

```markdown
# My Project

## Commands
- Run: `vivid .`

## Modules
- Core: Noise, Blur, HSV

## MCP Workflow
1. `get_pending_changes` - Check slider adjustments
2. Edit chain.cpp
3. `clear_pending_changes` - Confirm
4. `get_runtime_status` - Verify compilation

## Conventions
- Use setter pattern for operators
- Keep chains simple

## Boundaries
- Don't modify assets/ without asking
```

### BRIEF.md (Creative Vision)

User-owned, can be as long as needed. Contains:
- What you want to create
- Aesthetic goals
- Constraints

```markdown
# Vision

Audio-reactive 3D scene with geometric shapes that pulse to music.

## Aesthetic Goals
- Subtle, flowing animations
- Dark backgrounds with bright accents
- 1920x1080 resolution

## Constraints
- Must run at 60fps
- Target export for social media
```

### Why Two Files?

- **AGENTS.md** is read every session - keep it lean
- **BRIEF.md** captures creative intent - evolves with project
- Progress tracking is disposable - let AI regenerate task lists

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
4. **Update BRIEF.md**: Keep creative vision current as project evolves
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
