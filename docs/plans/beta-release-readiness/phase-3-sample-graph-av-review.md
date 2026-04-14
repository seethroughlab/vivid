# Phase 3: Sample Graph A/V Review

## Goal

Evaluate every sample graph for audio and video appropriateness, with the specific aim of finding unexpected or unwanted operator behavior before a synth-savvy non-programmer beginner opens the examples.

## Inputs

- Complete graph inventory from Phase 1
- Automated graph baseline from Phase 2
- Output analyzer and capture tooling where useful
- Audio output device and appropriate volume-limited monitoring setup
- Optional external devices for environment-specific graphs

## Steps

1. Start with intro and beginner-tagged graphs, then review the rest by folder.
2. For each graph, record:
   - Graph path
   - Intended purpose
   - Required devices/assets
   - First-load result
   - Audio result
   - Visual result
   - A/V relationship
   - Notable operator weirdness
   - Result classification
3. Listen to audio or A/V graphs for at least 30-60 seconds, longer for sequenced, looping, or slowly evolving graphs.
4. Watch GPU/filter/video graphs for enough time to observe animation, loops, and media playback behavior.
5. For A/V graphs, verify that the audio-to-visual relationship is legible and stable.
6. Use output analyzer metrics as supporting evidence when a failure needs quantification, such as silence, clipping, motion, brightness, or A/V reactivity.
7. Classify each graph:
   - `ready`
   - `minor polish`
   - `confusing but usable`
   - `blocking`
   - `environment skip`
8. For blocking graphs, either fix the graph/operator behavior or remove the graph from the beginner beta surface.

## Pass/Fail Criteria

Pass when every non-environment graph has a human A/V review result and every beginner/starter graph is `ready` or has only minor polish.

Fail on scary audio, unintended silence, stuck notes, harsh clipping, runaway feedback, black output, frozen output, broken media, accidental-looking shader artifacts, unreadable text, unintentional flashing/strobing, or an A/V graph whose relationship is unintelligible in the first-run path.

## Evidence to Record

- Reviewer
- Date and commit hash
- Audio device and approximate listening level
- Screenshot or capture path for visual failures
- Analyzer output path for quantified failures
- Notes for any subjective but important appropriateness concern

## Exit Criteria

Phase 3 exits when all sample graphs are reviewed, all starter/beginner blockers are fixed or removed from the beta surface, and remaining known limitations are documented.
