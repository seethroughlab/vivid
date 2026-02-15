# Vivid Remote Development: Telegram Setup Plan

## Goal

Message the bot from your phone with something like "add a bloom pass after feedback, keep it subtle" and have Claude Code edit the chain, build it, validate it with `vivid inspect` and `vivid check`, export a demo video, and send it back to you on Telegram — all without touching your computer.

## Architecture

```
Your Phone (Telegram)
    ↕
Telegram Servers
    ↕
linuz90/claude-telegram-bot (running on your PC)
    ↕
Claude Code SDK (with your existing Claude Code auth)
    ↕
Your filesystem: edits chain.cpp, runs vivid CLI commands
    ↕
Vivid: builds, inspects, checks, exports → video file
    ↕
Bot sends video back to Telegram → your phone
```

Everything runs on your local machine. Your PC needs to be on and the bot running. No cloud, no Docker, no separate API costs beyond your existing Claude Code subscription.

---

## Part 1: Bot Setup

### 1.1 Prerequisites

You need three things installed:

**Bun** (JavaScript runtime, used by the bot):
```bash
curl -fsSL https://bun.sh/install | bash
```

**Claude Code** already authenticated (you have this — just confirm by running `claude` in a terminal and verifying it connects).

**OpenAI API key** (optional, only needed if you want to send voice messages to the bot. Skip this initially and add later if you want it).

### 1.2 Create the Telegram Bot

1. Open Telegram on your phone
2. Search for `@BotFather` and start a conversation
3. Send `/newbot`
4. Choose a name (display name), e.g. "Vivid Dev"
5. Choose a username (must end in "bot"), e.g. `vivid_dev_bot`
6. BotFather gives you a token like `1234567890:ABC-DEF1234ghIkl-zyx57W2v1u123ew11` — save this
7. Send `/setcommands` to BotFather, select your bot, and paste:
```
start - Show status and user ID
new - Start a fresh session
resume - Resume last session
stop - Interrupt current query
status - Check what Claude is doing
```

### 1.3 Get Your Telegram User ID

Message `@userinfobot` on Telegram. It replies with your numeric user ID (e.g. `123456789`). Save this — it's used to restrict the bot to only you.

### 1.4 Clone and Configure the Bot

```bash
cd ~
git clone https://github.com/linuz90/claude-telegram-bot.git
cd claude-telegram-bot
bun install
cp .env.example .env
```

Edit `.env`:

```bash
# Required
TELEGRAM_BOT_TOKEN=1234567890:ABC-DEF...    # From BotFather
TELEGRAM_ALLOWED_USERS=123456789            # Your user ID from @userinfobot

# Point Claude Code at your Vivid projects
CLAUDE_WORKING_DIR=/path/to/vivid/projects

# Allow access to Vivid build directory and project files
ALLOWED_PATHS=/path/to/vivid,/path/to/vivid/projects

# Optional: voice transcription
# OPENAI_API_KEY=sk-...
```

The `CLAUDE_WORKING_DIR` is where Claude Code starts. Point it at your Vivid projects root, or at a specific project. Claude Code will be able to read and write files here and run shell commands.

### 1.5 Test It

```bash
cd ~/claude-telegram-bot
bun run src/index.ts
```

Open Telegram, find your bot, and send "hello". You should get a response from Claude. If that works, try "list the files in this directory" to confirm it can see your Vivid project.

### 1.6 Run It Persistently

You don't want to keep a terminal open. On macOS, the repo includes a LaunchAgent config. Alternatively, use a simple approach:

```bash
# Using screen
screen -S vivid-bot
cd ~/claude-telegram-bot
bun run src/index.ts
# Ctrl+A, D to detach. screen -r vivid-bot to reattach.

# Or using tmux
tmux new -s vivid-bot
cd ~/claude-telegram-bot
bun run src/index.ts
# Ctrl+B, D to detach. tmux attach -t vivid-bot to reattach.
```

On macOS, you can also use the included LaunchAgent to start on login — see the `launchagent/` directory in the repo.

---

## Part 2: Vivid-Side Preparation

The bot gives Claude Code shell access to your machine. Claude Code already has your Vivid MCP server and CLAUDE.md. But a few things will make the remote workflow much smoother.

### 2.1 Verify the CLI Tools Work End-to-End

Before involving Telegram at all, run through the exact sequence the bot will use. Open a terminal in your project directory and simulate what Claude would do:

```bash
# 1. Can Claude read the project?
cat vivid-project.json
vivid graph .
vivid params .

# 2. Can it build?
vivid build .

# 3. Can it inspect and check?
vivid inspect . --frame 10 --out /tmp/vivid-report
cat /tmp/vivid-report/inspection.json
ls /tmp/vivid-report/frame_*.png
vivid check . --frame 10

# 4. Can it export a video?
vivid export . --duration 5 --fps 30 --audio -o /tmp/vivid-preview.mp4
ls -la /tmp/vivid-preview.mp4
```

If any of these steps fail or produce incomplete output, fix them before setting up the bot. The most important one is step 4 — the exported mp4 is what gets sent to your phone.

### 2.2 Create a CLAUDE.md for Remote Workflow

Your project's CLAUDE.md (or a parent-level one in `CLAUDE_WORKING_DIR`) should teach Claude the remote iteration pattern. Add a section like:

```markdown
## Remote Development Workflow

When receiving requests via Telegram, follow this iteration pattern:

### Inner Loop (do silently, don't report every step)
1. Edit chain.cpp as needed
2. Run `vivid build .` — **check exit code, MUST be 0 before continuing.** If non-zero, read the error, fix chain.cpp, and rebuild. Never proceed with a failing build.
3. Run `vivid inspect . --frame 10 --out /tmp/vivid-report`
4. Read /tmp/vivid-report/inspection.json to verify the change looks correct
5. Run `vivid check . --frame 10` — if assertions fail, fix and retry
6. Repeat until satisfied

### Outer Loop (send results to user)
7. Run `vivid build .` — gate check (must pass before export)
8. Run `vivid export . --duration 10 --fps 30 --audio -o /tmp/vivid-preview.mp4`
9. Send the video file: respond with the file path so the bot can deliver it
10. Summarize what changed and what the inspection data showed

### Guidelines
- For parameter tweaks, go straight to inspect/check — no need for a full export every time
- For structural changes (adding/removing operators), always export a preview
- When the user says something subjective ("make it darker", "more energy"), translate that into concrete parameter changes using vivid params and the project manifest
- If unsure what the user means, ask — don't guess and export a 30-second video
- Keep exports short (5-10 seconds) for quick feedback. Only do longer exports when asked.
- Always mention which parameters you changed and by how much
```

### 2.3 Ensure File Sending Works ✅

**Done.** We added a `send_file` MCP tool to the bot ([PR #2](https://github.com/linuz90/claude-telegram-bot/pull/2)). Claude calls `send_file(file_path, caption?)` and the file is delivered via Telegram automatically — images as photos, videos as playable video, audio as audio, everything else as a document.

The implementation lives in:
- `send_file_mcp/server.ts` — MCP server (validates file, writes request JSON to `/tmp`)
- `src/handlers/streaming.ts` — `checkPendingSendFileRequests()` picks up requests, sends via Telegram API
- `src/session.ts` — detects `mcp__send-file` tool calls, triggers pickup (fire-and-forget, Claude continues)

To enable: add `send-file` to your `mcp-config.ts` (see `mcp-config.example.ts`).

**Tested end-to-end:** generated a test video with ffmpeg and sent it via the bot — arrived as a playable video in Telegram.

### 2.4 Video File Size Limits

Telegram has a 50MB file size limit for bots sending files. For a 10-second 960x540 30fps video, you'll be well under this. But for longer or higher-resolution exports, keep this in mind. Add to CLAUDE.md:

```markdown
### Export Settings for Telegram
- Quick preview: --duration 5 --fps 30 --audio (small file, fast)
- Standard preview: --duration 15 --fps 30 --audio (good quality, still under 50MB)
- Note: --resolution flag currently crashes on export — omit it for now
- Keep exports short to stay under Telegram's 50MB bot file limit
- For agent workflows: use `--exit-on-error` flag when running vivid interactively to exit immediately on compile errors instead of hanging
```

### 2.5 Add a `vivid preview` Convenience Command (Optional)

If you find yourself constantly using the same export flags, consider adding a simple wrapper script or alias that the LLM can call:

```bash
#!/bin/bash
# ~/bin/vivid-preview
# Quick preview export with Telegram-friendly defaults
PROJECT="${1:-.}"
DURATION="${2:-10}"
OUTPUT="${3:-/tmp/vivid-preview.mp4}"

vivid export "$PROJECT" \
    --duration "$DURATION" \
    --resolution 960x540 \
    --fps 30 \
    --output "$OUTPUT"

echo "$OUTPUT"
```

Then in CLAUDE.md: "Use `vivid-preview` for quick Telegram exports. Usage: `vivid-preview [project] [duration] [output-path]`"

---

## Part 3: The Workflow in Practice

### What a Typical Session Looks Like

**You (on phone, Telegram):**
> Add a chromatic aberration effect after the bloom. Subtle, just on the edges.

**Bot (Claude Code, silently):**
1. Reads chain.cpp, sees the current chain: noise → feedback → bloom → hsv → composite
2. Edits chain.cpp to insert ChromaticAberration between bloom and hsv
3. Runs `vivid build .` — checks exit code is 0 (success). If non-zero, reads error and fixes before continuing.
4. Runs `vivid inspect . --duration 2 --samples 3 --out /tmp/vivid-report`
5. Reads inspection.json — chromatic aberration operator is active, output contrast still good
6. Runs `vivid check . --duration 2` — all assertions pass
7. Runs `vivid build .` — gate check before export (exit 0)
8. Runs `vivid export . --duration 8 --fps 30 --audio -o /tmp/preview.mp4`

**Bot (responds on Telegram):**
> Added ChromaticAberration between bloom and hsv with offset 0.003 (subtle). Assertions all pass, contrast held steady at 0.21. Here's the preview:
> 📎 preview.mp4

**You:**
> Nice but push the offset up a bit, I want to see it more

**Bot (silently):**
1. Changes ChromaticAberration offset from 0.003 to 0.008
2. Runs inspect → checks offset is applied, still no clipping
3. Runs check → pass
4. Exports 8 seconds

**Bot:**
> Bumped offset from 0.003 to 0.008. Here's the comparison:
> 📎 preview.mp4

### Multi-Project Navigation

If your `CLAUDE_WORKING_DIR` points to a projects parent directory, you can switch between projects:

**You:** "switch to the cult-visuals-act-2 project"
**You:** "what's the chain look like in this one?"
**You:** "add the same chromatic aberration effect we used in act-1"

Claude Code can `cd` between projects, read their respective CLAUDE.md files and manifests, and transfer patterns between them.

---

## Part 4: Troubleshooting & Iteration

### If the Bot Can't Send Video Files

**Resolved.** The `send_file` MCP tool handles this. If it's not working, check:
- `send-file` is listed in your `mcp-config.ts`
- Bot logs show "Loaded N MCP servers from mcp-config.ts" on startup
- `TELEGRAM_CHAT_ID` is being set (the bot does this automatically)

### If Sessions Get Confused

Use `/new` in Telegram to start a fresh Claude Code session. Use `/resume` to pick up where you left off. If Claude seems to have lost context about the project, send "read CLAUDE.md and vivid-project.json to refresh your context."

### If Builds Are Slow

The first build after a change requires full compilation. If this is taking too long over Telegram (you're waiting 30+ seconds for a response), consider:
- Pre-building before starting a remote session so incremental builds are fast
- Using `vivid inspect` without the export step for quick iteration, only exporting when you want to see video

### If You Hit Rate Limits

Claude Code subscription has usage limits. Long sessions with many inspect/export cycles consume tokens. Keep CLAUDE.md concise, keep exports short, and use `/new` to start fresh sessions when switching tasks to avoid bloated context.

---

## Part 5: Backup Option — RichardAtCT/claude-code-telegram

If the linuz90 bot doesn't work out (file sending issues, stability problems, etc.), the backup is RichardAtCT/claude-code-telegram. It's a more feature-rich alternative with 257 stars and active maintenance.

### Key Differences

| Feature | linuz90 | RichardAtCT |
|---------|---------|-------------|
| Core approach | Thin SDK wrapper, minimal | Full-featured, more structured |
| File sending | Basic | May handle files better |
| Directory navigation | Via Claude Code | Built-in `/cd`, `/ls`, `/pwd` commands |
| Session management | `/new`, `/resume` | `/new`, `/continue`, `/end`, `/export` |
| Git integration | Via Claude Code | Built-in `/git` commands |
| Auth | Claude Code CLI auth or API key | Whitelist + optional token |
| Agentic mode | Always agentic | Toggle between agentic and command mode |
| Setup complexity | Lower | Slightly higher |

### Setup (if needed)

```bash
git clone https://github.com/RichardAtCT/claude-code-telegram.git
cd claude-code-telegram
npm install   # or bun install
cp .env.example .env
```

Edit `.env`:
```bash
TELEGRAM_BOT_TOKEN=1234567890:ABC-DEF...
TELEGRAM_BOT_USERNAME=vivid_dev_bot
APPROVED_DIRECTORY=/path/to/vivid/projects
ALLOWED_USERS=123456789
```

```bash
make run
```

Same CLAUDE.md workflow applies — the bot is just the transport layer. The iteration pattern (edit → build → inspect → check → export → send) is identical regardless of which bot you use.

---

## Part 6: Checklist

### One-Time Setup
- [x] Install Bun (`curl -fsSL https://bun.sh/install | bash`)
- [x] Create Telegram bot via @BotFather, save token — **@Charles555Bot**
- [x] Get your Telegram user ID via @userinfobot
- [x] Clone linuz90/claude-telegram-bot, run `bun install` — cloned to `/Users/jeff/Developer/claude-telegram-bot`
- [x] Configure `.env` with token, user ID, and Vivid working directory
- [x] Verify Claude Code is authenticated (`claude` in terminal)

### Vivid Preparation
- [x] Verify `vivid build .` works from your project directory
- [x] Verify `vivid inspect . --frame 10 --out /tmp/test-report` produces JSON + PNGs
- [x] Verify `vivid check . --frame 10` runs assertions
- [x] Verify `vivid export . --duration 5 --audio -o /tmp/test.mp4` produces a playable video (note: `--resolution` flag crashes, omit it)
- [x] Update CLAUDE.md with remote workflow instructions (in AGENTS.md per project convention)
- [x] Confirm vivid params outputs parameter descriptions and ranges
- [x] Confirm vivid-assertions.json covers the key creative constraints

### First Test
- [x] Start the bot: `bun run src/index.ts`
- [x] Send "hello" on Telegram — confirm response
- [x] Verify video file arrives on Telegram (tested with ffmpeg-generated test video via `send_file` MCP)
- [ ] Send "run vivid params ." — confirm it reads your project
- [ ] Send "run vivid inspect . --frame 10 --out /tmp/test" — confirm JSON output
- [ ] Send "change noise.scale to 8.0, rebuild, and export a 5 second preview" — confirm full loop

### Running Persistently
- [ ] Set up screen/tmux/LaunchAgent so the bot survives terminal close
- [ ] Test that the bot responds after your terminal is closed
