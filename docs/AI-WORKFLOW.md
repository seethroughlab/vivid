# AI-Assisted Development Workflow

How to work effectively with AI coding assistants (Claude Code, Copilot, Cursor, etc.) on Vivid projects.

## Project Files

Every Vivid project can include two documentation files for AI assistants:

```
my-project/
├── chain.cpp       # Your visual code
├── AGENTS.md       # Operational context (required)
├── BRIEF.md        # Creative vision (recommended)
└── assets/         # Images, audio, video
```

### AGENTS.md

**Purpose:** Operational context that AI assistants need every session.

Created automatically by `vivid new`. Keep it **under 60 lines** - AI re-reads this every conversation, so bloat hurts performance.

**What to include:**
- Commands (how to run the project)
- Modules enabled
- MCP workflow steps
- Code conventions
- Boundaries (what not to modify)

**What NOT to include:**
- Progress notes or task lists (these go stale)
- Creative vision (put in BRIEF.md)
- Lengthy explanations

**Example:**
```markdown
# My Audio Visualizer

## Commands
- **With devtools**: `vivid . --show-ui`
- **Minimal output**: `vivid .`

## Modules
- Core: Noise, Blur, HSV, Bloom
- Audio: AudioIn, Levels, BandSplit

## MCP Workflow
1. `get_pending_changes` - Check slider adjustments
2. Edit chain.cpp with new values
3. `clear_pending_changes` - Confirm
4. `get_runtime_status` - Verify compilation

## Conventions
- Use setter pattern: `noise.scale = 4.0f;`
- Keep chains simple

## Boundaries
- Don't modify assets/ without asking
```

### BRIEF.md

**Purpose:** Creative vision for your project - what you're building and why.

User-owned and can be as long as needed. Update it as your vision evolves.

**What to include:**
- Vision statement (what you're creating)
- Aesthetic goals (visual style, mood)
- Technical constraints (resolution, performance)
- Reference notes or inspiration

**Example:**
```markdown
# Vision

A hypnotic audio visualizer that responds to bass frequencies with pulsing geometric shapes.

## Aesthetic Goals
- Deep space theme with dark backgrounds
- Neon accent colors (cyan, magenta, gold)
- Smooth, flowing motion - nothing jarring

## Constraints
- 1920x1080 for social media export
- Must run at 60fps on M1 Mac

## Notes
- Inspired by Tron Legacy aesthetics
- Bass should drive size, treble drives color
```

---

## MCP Integration (Claude Code)

Vivid includes an MCP server for live integration with Claude Code. This enables a powerful workflow where you adjust visual parameters with sliders and Claude persists your changes to code.

### Setup

Add to your Claude Code MCP config (`~/.claude.json`):

```json
{
  "mcpServers": {
    "vivid": {
      "command": "/path/to/vivid",
      "args": ["mcp"]
    }
  }
}
```

### The Slider-to-Code Workflow

1. **Start your project** with devtools: `vivid . --show-ui`
2. **Press Tab** to toggle all panels, or `Cmd+4` to show the node graph
3. **Select a node** and **adjust sliders** in the Inspector - see changes in real-time
4. **Ask Claude** to persist your changes:
   - Claude calls `get_pending_changes` to see what you adjusted
   - Claude edits chain.cpp with the new values
   - Claude calls `clear_pending_changes` to confirm
   - Claude calls `get_runtime_status` to verify compilation succeeded

### Available MCP Tools

| Tool | Description |
|------|-------------|
| `get_runtime_status` | Check if compilation succeeded, see errors |
| `get_pending_changes` | See slider values waiting to be saved |
| `clear_pending_changes` | Confirm changes were applied to code |
| `capture_frame` | Capture current frame to PNG |
| `set_param` | Set a parameter value immediately |
| `orbit_camera` | Position camera for 3D scenes |
| `search_docs` | Search Vivid documentation |

---

## Best Practices

### 1. Keep AGENTS.md Operational
Don't put progress notes, task lists, or "what I'm working on" in AGENTS.md. These become stale and waste AI context. Let the AI generate task lists when needed - they're disposable.

### 2. Separate Vision from Operations
- **AGENTS.md** = How to work on this project (stable)
- **BRIEF.md** = What we're building (evolves)

### 3. Trust Hot-Reload
When Claude edits chain.cpp, Vivid automatically recompiles and reloads. You'll see changes instantly. If there's a compile error, Claude should check `get_runtime_status` and fix it.

### 4. Use the MCP Workflow
The slider → get_pending_changes → edit → clear → verify workflow is faster than describing changes in words. Adjust visually, let Claude persist.

### 5. Be Specific in BRIEF.md
"Make it look cool" is less helpful than "Neon colors on dark background, bass drives scale, smooth motion".

---

## Why Two Files?

Based on research into AI coding workflows (GitHub's analysis of 2,500+ AGENTS.md files, Ralph Wiggum methodology, etc.), separating concerns improves AI performance:

| File | Purpose | Volatility | Size |
|------|---------|------------|------|
| AGENTS.md | Operational | Stable | ~60 lines |
| BRIEF.md | Creative vision | Evolves | Any length |
| PLAN.md | Task tracking | Disposable | AI-generated |

AI models can reliably follow ~150-200 instructions. Keeping AGENTS.md lean leaves more "thinking room" for actual work.

---

## Examples

Every example in `modules/*/examples/` includes both AGENTS.md and BRIEF.md files. Good ones to study:

- `modules/vivid-core/examples/feedback/` - Classic feedback effect
- `modules/vivid-audio/examples/drum-machine/` - Audio with visuals
- `modules/vivid-render3d/examples/3d-basics/` - 3D scene setup
- `projects/getting-started/03-parameters/` - MCP workflow tutorial

---

## Creating a New Project

```bash
vivid new my-project
```

This creates:
- `chain.cpp` - Your visual code
- `AGENTS.md` - Pre-filled with commands and MCP workflow
- `BRIEF.md` - Template for your creative vision
- `.claude/settings.local.json` - Pre-authorized MCP permissions

Edit BRIEF.md to describe what you want to create, then start coding!
