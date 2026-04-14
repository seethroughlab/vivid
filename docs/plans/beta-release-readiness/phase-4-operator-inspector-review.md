# Phase 4: Operator Inspector Review

## Goal

Evaluate the inspector of every registered operator for usability from the perspective of a synth-savvy non-programmer beginner.

## Inputs

- Registered operator inventory from Phase 1
- Existing inspector screenshot/capture tooling
- Minimal graphs or generated review graphs for selecting each operator
- Operator source docs and metadata where available

## Steps

1. For every registered operator, create or load a graph that selects the node and exposes its inspector.
2. Capture the inspector or record the live-review artifact path.
3. Review whether the inspector answers:
   - What does this operator do?
   - Which controls should a beginner try first?
   - Which values are safe?
   - What should change in the output when a control moves?
4. Test relevant control types:
   - Slider
   - Typed numeric or string value
   - Dropdown
   - Toggle
   - Color picker
   - XY pad
   - File picker
   - Custom rich widget
   - MIDI mapping controls
   - Preset controls
   - Modulation controls
5. Record layout problems, missing labels, confusing names, dangerous defaults, and controls that fail to update runtime state.
6. Classify each operator:
   - `ready`
   - `minor copy/layout polish`
   - `confusing but usable`
   - `blocking`
7. Fix or remove from the beta surface any blocking inspector issue in starter/beginner paths.

## Pass/Fail Criteria

Pass when every registered operator has an inspector review result and all operators used by starter/beginner graphs are `ready` or have only minor polish.

Fail on clipped or overlapping controls, broken interaction, unsafe default ranges, missing critical labels, labels that require programmer knowledge for basic use, values that do not update the runtime, or custom inspectors that fail to fit or explain themselves.

## Evidence to Record

- Operator registered name
- Domain
- Review graph path
- Selected node ID
- Screenshot/capture path
- Control types exercised
- Result classification
- Follow-up task or fix link

## Exit Criteria

Phase 4 exits when every registered operator inspector has been reviewed and all beta-path inspector blockers are fixed, hidden, or explicitly documented as out of scope.
