# ADR-0036: Free Plugin Onboarding Is Curated

Status: accepted

Date: 2026-07-26

Related research: [Native Instrument Candidate Pass](../research/native-instrument-candidates.md).

## Context

Vivid 4 is plugin-first for music authoring. Users are less likely to write their own audio nodes
than their own visual nodes, so tutorials should embrace existing free instruments and effects
instead of pretending every beginner sound path must be native-only.

At the same time, plugin setup can easily become the first-run failure point. A missing plugin,
wrong format, moved download link, architecture mismatch, or confusing install path can make the
beginner tutorial feel broken even when Vivid itself is working.

## Decision

The website will include a curated free-plugin page. Surge XT is the assumed beginner instrument for
Vivid tutorials until Vivid has a strong native instrument; other required plugins must be verified
and documented before the tutorial is published.

Candidate plugins to evaluate first:

- Surge XT: CLAP/VST3 instrument and effects. Assumed installed for beginner tutorials.
- Vital Basic: free wavetable synth.
- Dexed: free FM synth.
- TAL-NoiseMaker: free subtractive synth.
- Valhalla Supermassive: free effect.
- BPB Cassette Drums: free drum instrument, with special attention to its extra data-folder
  installation friction.

Before a plugin is marked tutorial-required, verify:

- Current download URL.
- License/free-tier status.
- macOS format availability: VST3, CLAP, or both.
- Apple Silicon and Intel compatibility.
- Expected install path.
- Vivid scan/discovery behavior.
- Instrument/effect classification.
- Preset loading behavior, if used.
- Missing-plugin recovery path in the tutorial and in saved projects.

## Alternatives Considered

- **Avoid plugin dependencies in beginner tutorials.** Rejected. It conflicts with Vivid's
  plugin-first music direction and would overemphasize native audio operators.
- **Let tutorials mention arbitrary popular plugins.** Rejected. Onboarding needs a tested, stable,
  supportable list.
- **Require paid/commercial plugins for showcases.** Rejected for beginner material. Showcase demos
  may optionally note paid-plugin variants later, but the public tutorial path starts with free
  plugins.

## Consequences

- The tutorial path can depend on plugins, but only through explicit, tested requirements.
- Plugin documentation becomes part of release readiness.
- The app may need better missing-plugin diagnostics, scan status, and recovery UX before the first
  plugin-dependent tutorial can ship.
- `vivid-wavetable` should be treated as the later first-party native synthesis package path, not as
  a reason to remove Surge XT from the first beginner tutorial.
