# Inspector UI Audit And Redesign Plan

## Summary

Treat the current Inspector issues as a systemic UI problem, not a set of one-off operator fixes.

The main problems are:
- weak layout metadata
- fixed-width inspector with overly dense content
- generic column packing applied to widget types with very different size needs
- recent adaptive-column logic that preserves geometry but not semantic intent

The fix should be a small redesign of the Inspector layout model, not more patches on top of the current `layout_columns` behavior.

## Key Changes

### 1. Replace generic N-column packing with a constrained inspector layout model
Keep operator metadata, but change how the Inspector interprets it.

Recommended v1.5 model:
- supported row modes:
  - `full`
  - `two_up`
  - `compound`
- do not support arbitrary `3` or `4` column inspector rows in the default narrow inspector
- treat old `layout_columns >= 3` metadata as legacy hints that are normalized into:
  - `two_up`, or
  - stacked full-width rows

This removes most of the impossible geometry.

### 2. Make layout semantic, not arithmetic
Instead of “column count + column index” being the main driver, define rendering around widget families.

Recommended rules:
- knobs:
  - `two_up` max
- dropdowns:
  - full-width by default
- sliders:
  - full-width by default, `two_up` only for compact numeric controls
- color / XY / compound controls:
  - full-width only
- source labels / semantic hints:
  - optional secondary text, clipped or suppressed in compact layouts

This keeps operator intent readable instead of just mathematically packed.

### 3. Redesign the Inspector visual hierarchy
Split the Inspector into clearer vertical sections:

- Header
- Primary Controls
- Secondary Controls
- Bindings / References
- Technical / Outputs

Recommended design changes:
- primary params get more space and cleaner rhythm
- secondary metadata is visually quieter
- role bindings and referenced-by look like separate panels, not continuation rows
- outputs and resolution move to a lower-priority technical section

This reduces the “everything is one dense stack” feeling.

### 4. Widen the default Inspector and allow future resize
Increase the default width from `320` to a more realistic working width, likely in the `380-420` range.

For this phase:
- pick one wider default
- update widget math accordingly
- do not add drag-resize yet unless it falls out naturally

This gives the layout system room to breathe before deeper responsiveness work.

### 5. Remove modulo-based reflow and replace it with explicit fallback
The current adaptive logic in `InspectorLayout` should not remap columns with modulo.

Replace it with explicit fallback behavior:
- if a requested row does not fit:
  - convert it to `two_up`, preserving order
  - or convert it to stacked full-width rows
- do not preserve the old column indices when the row shape changes
- the fallback must keep semantic order obvious to the user

This avoids the “controls are now in weird columns” regression.

### 6. Add clipping/truncation as a safety net, not the primary design
Keep clipping and truncation, but use them only to prevent rare overflow.

Apply to:
- param labels
- value text
- semantic hint lines
- source labels
- badges when necessary

But do not rely on clipping to make an otherwise over-dense layout acceptable.

### 7. Audit operators and simplify the worst offenders after the UI redesign
Once the Inspector model is fixed, audit operators using dense layouts:
- `LFO`
- `Envelope`
- `MSEG`
- `StepSeq`
- `Particles`
- `Instanced Shapes`
- `Flocking`
- `ParametricEQ`
- `RandomSH`
- `Macro`

Convert them to the new intended Inspector grammar:
- mostly full-width
- selective `two_up`
- compound widgets where meaningful

This is cleanup after the system change, not the first step.

## Public Interfaces / Types

Change the effective contract of inspector layout metadata:

- current `layout_columns` / `layout_column_index` become legacy/advisory
- the Inspector UI normalizes them into a limited set of supported row patterns
- future direction:
  - add clearer layout hints for `full`, `two_up`, `compound`
  - avoid exposing arbitrary N-column authoring in the long term

No backward-compat preservation is needed if you want to clean this up properly during development.

## Test Plan

### Visual/manual checks
Verify with:
- `graphs/gpu/instanced_shapes_demo.json`
- an `LFO`
- `Envelope`
- `MSEG`
- `StepSeq`
- one GPU operator with grouped params
- one operator with role bindings and referenced-by content

Confirm:
- no overlap
- no semantically strange column pairings
- improved readability at rest
- role-binding panels feel separate and intentional

### UI/layout regression checks
Add focused tests for:
- fallback from over-dense operator layout hints
- preservation of control order under fallback
- clipping/truncation safety behavior
- correct stacking of:
  - params
  - custom inspector content
  - role bindings
  - referenced-by
  - outputs

## Assumptions

- The current Inspector should prioritize readability over density.
- Arbitrary `3`/`4`-column layouts are not worth preserving in the default inspector.
- Role Bindings exposed the weakness, but the right fix is a broader Inspector redesign rather than Role Binding-specific patching.
