# Gate 1 measurement — can a returning creator steer in two minutes?

Status: protocol ready; **result pending** (fill in the Result section after the session)

Date: 2026-09-19

The acceptance evidence ADR-0064 asks for: *use real audio and moving visuals to compare a baseline
and two alternatives; measure whether a returning creator can understand the change, choose or
reject it, and leave passage-specific feedback within two minutes without reading a transcript.*
This is a measurement, not a demo: the creator has **not** seen the candidates before, and the
person running the protocol says nothing until the clock stops.

## Setup (the agent, before the creator sits down)

1. Build `main` and launch Vivid. Keep the display awake (`caffeinate -dimsu`) and the window frontmost
   while exporting (a sleeping display or occluded window collapses the frame loop).
2. Publish the round on a **writable copy** of the song sketch:
   ```sh
   uv run --directory mcp python gate1/publish_review.py \
       --project ~/Music/vivid-gate1/song-sketch --from-example --scene 0 --bars 8
   ```
   This snapshots the baseline, records the brief, renders the baseline excerpt, renders two
   directions on isolated copies (snapshotted into the original's `work/`), and publishes one review
   item. Nothing in `work/` is hand-written.
3. Leave the app in **Create** on the original project, with the pending badge showing `1` on the
   `Create | Review` switch. Do not open Review. Quit any MCP client; the creator must not read a
   transcript.
4. Start a stopwatch when the creator first touches the mouse or keyboard.

## The session (the creator)

Scenario given aloud, once, before the clock starts (this is the brief the creator gave earlier —
it is also on screen under *Direction & protections*):

> "Yesterday you asked for the chorus to feel more expansive, with the visuals opening up, and for the
> drums and the chord part to stay as they are. Two directions came back. Decide."

Then nothing. The creator switches to Review and does whatever they do. Stop the clock when they say
they are done, or at **2:00**, whichever comes first.

## What is recorded (the observer, silently)

| # | Question | Record |
|---|---|---|
| 1 | Did they find the decision without help? | yes / no · seconds until the review item was on screen |
| 2 | Did they play the comparison? Did they switch between Baseline / A / B while it played? | yes / no · which sources were heard · used tiles, keys, or both |
| 3 | Could they say what changed, in their own words, without looking at anything but the screen? | verbatim |
| 4 | Did they make a choice — Use / Keep / Revise / Dismiss / Neither? | which · seconds |
| 5 | Did they leave a passage-specific comment (a bar, not "the whole thing")? | yes / no · verbatim · the bar the record carries |
| 6 | Did they open anything that was not the review (Create, a graph, a log) to understand the change? | what |
| 7 | Did anything confuse them? Asked aloud or visibly hesitated? | verbatim / where |
| 8 | Total time to done | m:ss |

Pass = 1, 3, 4 and 5 all yes within 2:00 and nothing in 6 was *needed* to answer 3.
Anything else is a fail that names the cause — that is the useful outcome.

## The follow-up probes (after the clock, in this order)

These are the ADR-0064 edge cases; each is one action and one observation.

- **No agent connected.** Nothing was connected during the session. Confirm the choice and the comment
  are in `work/feedback.jsonl` and `work/reviews/<id>.json` (`resolution`), written by the app alone.
- **Undo.** ⌘Z. Did the *Current version* card and the *Since your last review* list tell the truth
  (pointer back, candidate back to *kept*, `promotion_undone` event)? Redo. Same question.
- **Feedback on an older preview.** Select the resolved review, comment at a bar. Does the record target
  the candidate's version, not the promoted document?
- **Neither.** (If they chose one.) Ask: "if you had liked neither?" — do they find *Neither — dismiss
  all* without help?
- **Back to Create.** Does the session grid / graph come back exactly as they left it (selection,
  dock, preview)?
- **Save prompt.** ⌘Q. The promotion left the document dirty; the prompt should appear. Don't Save.

## Result

_Fill in after the session. Copy the table with the observations, the verbatim answers to 3, 5 and 7,
and one paragraph: what the layout gets right, what it gets wrong, and what changes before the exact
layout is fixed (ADR-0064: "exact layout should follow this interaction proof")._

- Date / build:
- Creator:
- Pass / fail:
- Observations:
- Probes:
- Changes to make:
