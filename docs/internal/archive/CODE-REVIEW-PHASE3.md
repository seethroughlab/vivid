# Code Review Phase 3: Operator Contract + Seed Operator Exploration

## Purpose

This note is the Phase 3 operator-contract artifact for the Vivid code review process described in [CODE_REVIEW.md](/Users/jeff/Developer/vivid/docs/internal/CODE_REVIEW.md).

The goal of this phase is to understand the operator authoring contract first, then identify representative seed operators that define current patterns. This is still exploration rather than audit. It records:

- how operators are declared and exported
- what the current operator API appears to expect from authors
- which seed operators are good representatives of current domain patterns
- where newer features such as custom ports, strings, shared handles, media sessions, and data-driven filters show up in practice

This note does not attempt to review every operator or judge per-operator quality.

## Operator Authoring Model

At a high level, Vivid’s operator model appears to be:

- C++ struct/class-based operator definitions
- parameter metadata and port metadata declared directly in code
- a generated C ABI boundary via [VIVID_REGISTER](/Users/jeff/Developer/vivid/src/operator_api/operator.h)
- domain-specific execution through one of:
  - `ControlOperatorBase`
  - `AudioOperatorBase`
  - `GpuOperatorBase`
- runtime-owned process contexts supplying params, input values, spread/string/custom inputs, file params, and shared-handle access

The authoring contract is split between:

- [operator.h](/Users/jeff/Developer/vivid/src/operator_api/operator.h)
- [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h)
- [audio_operator.h](/Users/jeff/Developer/vivid/src/operator_api/audio_operator.h)
- [gpu_operator.h](/Users/jeff/Developer/vivid/src/operator_api/gpu_operator.h)
- [data_driven_filter.h](/Users/jeff/Developer/vivid/src/operator_api/data_driven_filter.h)
- [child_op.h](/Users/jeff/Developer/vivid/src/operator_api/child_op.h)
- custom-type support in [type_id.h](/Users/jeff/Developer/vivid/src/operator_api/type_id.h) and [port_type_registry.h](/Users/jeff/Developer/vivid/src/operator_api/port_type_registry.h)

## Core Authoring Contract

### 1. Parameter declaration

Operators declare params as typed `Param<T>` members in [operator.h](/Users/jeff/Developer/vivid/src/operator_api/operator.h), usually one of:

- `Param<float>`
- `Param<int>`
- `Param<bool>`
- `Param<FilePath>`
- `Param<TextValue>`

The author then exposes them through `collect_params(std::vector<ParamBase*>&)`.

The param model appears to support three layers of meaning:

- runtime value/default/min/max
- inspector layout metadata (`display_hint`, grouping, row layout)
- semantic metadata (`semantic_tag`, `semantic_shape`, `semantic_unit`, `semantic_intent`)

Representative examples:
- [LFO](/Users/jeff/Developer/vivid/operators/control/lfo/lfo.h)
- [Smooth](/Users/jeff/Developer/vivid/operators/control/smooth/smooth.h)
- [Filter](/Users/jeff/Developer/vivid/operators/audio/filter/filter.cpp)

### 2. Port declaration

Operators expose ports through `collect_ports(std::vector<VividPortDescriptor>&)`.

The current built-in port types visible in [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h) are:

- `VIVID_PORT_FLOAT`
- `VIVID_PORT_AUDIO`
- `VIVID_PORT_SPREAD`
- `VIVID_PORT_STRING`
- `VIVID_PORT_STRING_SPREAD`
- `VIVID_PORT_TEXTURE`

The contract now also supports custom port types via:

- stable custom type ids in [type_id.h](/Users/jeff/Developer/vivid/src/operator_api/type_id.h)
- `VIVID_CUSTOM_REF_PORT` / `VIVID_CUSTOM_VALUE_PORT`
- runtime registration metadata in [port_type_registry.h](/Users/jeff/Developer/vivid/src/operator_api/port_type_registry.h)

Representative examples:
- string spread flow in [FolderList](/Users/jeff/Developer/vivid/operators/control/folder_list/folder_list.cpp) and [StringSelect](/Users/jeff/Developer/vivid/operators/control/string_select/string_select.cpp)
- custom ref output in [MidiInput](/Users/jeff/Developer/vivid/operators/control/midi_input/midi_input.cpp)
- media-stream custom ref flow in [MovieLoaded](/Users/jeff/Developer/vivid/operators/gpu/movie_loaded/movie_loaded.cpp), [MovieVideoOut](/Users/jeff/Developer/vivid/operators/gpu/movie_video_out/movie_video_out.cpp), and [MovieAudioOut](/Users/jeff/Developer/vivid/operators/audio/movie_audio_out/movie_audio_out.cpp)

### 3. Domain execution

The process contract differs by domain:

- control operators implement `process(const VividProcessContext*)`
- audio operators implement `process_audio(const VividAudioContext*)`
- GPU operators implement `process_gpu(const VividGpuContext*)`

The contexts in [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h) and [gpu_operator.h](/Users/jeff/Developer/vivid/src/operator_api/gpu_operator.h) suggest:

- control operators work mainly with scalar/spread/string/custom port snapshots on the main thread
- audio operators get planar audio buffers plus control-domain cross-thread snapshots and shared-handle service access
- GPU operators get device/queue/encoder/output texture state plus control-domain inputs and custom/shared-handle inputs

### 4. Export / ABI boundary

[VIVID_REGISTER](/Users/jeff/Developer/vivid/src/operator_api/operator.h) is the main ABI bridge.

It appears to:

- generate `vivid_abi_version`
- generate `vivid_descriptor`
- generate create/destroy functions
- generate domain dispatch functions
- synchronize runtime param/file-param values into the C++ operator instance before dispatch
- infer domain from base class before falling back to port inference

Optional extension macros also exist for:

- thumbnails: `VIVID_THUMBNAIL`
- inspector overrides: `VIVID_INSPECTOR`, `VIVID_INSPECTOR_FULL_MODE`
- custom type export metadata: `VIVID_DESCRIBE_REF_TYPE`, `VIVID_DESCRIBE_REF_TYPES2`

### 5. Alternate authoring paths

Two notable alternate authoring patterns exist beyond the basic single operator struct:

- **Header-embeddable control operators** via [ChildOp](/Users/jeff/Developer/vivid/src/operator_api/child_op.h)
- **Data-driven GPU filters** via [WgslFilterBase](/Users/jeff/Developer/vivid/src/operator_api/wgsl_filter.h) and [DataDrivenFilter](/Users/jeff/Developer/vivid/src/operator_api/data_driven_filter.h)

These appear to support:

- composite control operators that internally embed reusable control ops
- self-describing WGSL filters and user-defined filter variants without writing full custom GPU operator logic

## Seed Operator Taxonomy

This section groups representative operators by pattern rather than by completeness.

### Control domain patterns

#### Simple scalar control generator

- [LFO](/Users/jeff/Developer/vivid/operators/control/lfo/lfo.h)

What it represents:
- minimal control operator shape
- typed params + semantic metadata
- one input / one output scalar flow
- time-dependent state in the operator instance

#### Stateful smoothing / custom thumbnail

- [Smooth](/Users/jeff/Developer/vivid/operators/control/smooth/smooth.h)

What it represents:
- control-state persistence between frames
- inspector layout hints
- operator-supplied thumbnail rendering

#### Composite control operator

- [ModulatedGain](/Users/jeff/Developer/vivid/operators/control/modulated_gain/modulated_gain.cpp)

What it represents:
- `ChildOp<T>` composition
- control-domain internal operator chaining
- “composite operator” pattern without adding new graph nodes

#### String-first control operators

- [FolderList](/Users/jeff/Developer/vivid/operators/control/folder_list/folder_list.cpp)
- [StringSelect](/Users/jeff/Developer/vivid/operators/control/string_select/string_select.cpp)

What they represent:
- first-order string and string-spread ports
- cache/refresh behavior around filesystem inputs
- string output lifetimes managed from operator-owned `std::string` storage

#### Interactive input operator

- [Keyboard](/Users/jeff/Developer/vivid/operators/control/keyboard/keyboard.cpp)

What it represents:
- use of `VividInputState`
- session-time input-event interpretation in control domain

#### Custom ref output / external device integration

- [MidiInput](/Users/jeff/Developer/vivid/operators/control/midi_input/midi_input.cpp)

What it represents:
- runtime custom port emission from a control operator
- mixed scalar + spread + custom-ref outputs in one operator
- integration with an external device library while preserving graph-facing typed outputs

### Audio domain patterns

#### Canonical simple audio source

- [Oscillator](/Users/jeff/Developer/vivid/operators/audio/oscillator/oscillator.cpp)

What it represents:
- straightforward `AudioOperatorBase` implementation
- audio output buffer writing
- float CV inputs through `input_float_values`
- persistent DSP state in the operator instance

#### Canonical stateful DSP effect

- [Filter](/Users/jeff/Developer/vivid/operators/audio/filter/filter.cpp)

What it represents:
- audio in/out + float CV inputs
- stateful per-sample processing
- richer param layout and semantic metadata
- optional operator-supplied thumbnail behavior in an audio operator

#### Session/shared-handle audio consumer

- [MovieAudioOut](/Users/jeff/Developer/vivid/operators/audio/movie_audio_out/movie_audio_out.cpp)

What it represents:
- strict custom-ref-driven operator input
- shared-handle resolution in audio domain
- audio-thread/main-thread coordination inside one operator
- use of shared operator-layer media session types from [media_session.h](/Users/jeff/Developer/vivid/operators/shared/media_session/media_session.h)

### GPU domain patterns

#### Full custom GPU operator

- [Noise](/Users/jeff/Developer/vivid/operators/gpu/noise/noise.cpp)

What it represents:
- self-contained shader-heavy GPU operator
- custom pipeline setup and full `process_gpu` ownership
- a useful example of the “write a real GPU operator from scratch” path

#### Texture-loading / resource-backed GPU source

- [TextureLoader](/Users/jeff/Developer/vivid/operators/gpu/texture_loader/texture_loader.cpp)

What it represents:
- file-backed GPU operator
- texture lifetime/recreation logic
- image decode + upload path
- output-size negotiation back to the runtime

#### Data-driven filter base path

- [WgslFilterBase](/Users/jeff/Developer/vivid/src/operator_api/wgsl_filter.h)
- [DataDrivenFilter](/Users/jeff/Developer/vivid/src/operator_api/data_driven_filter.h)

What it represents:
- reusable GPU operator scaffolding built around WGSL fragment code
- hot-reloadable filter source path model
- dynamic params and ports for user-defined filters

#### Shared-session GPU source/controller

- [MovieLoaded](/Users/jeff/Developer/vivid/operators/gpu/movie_loaded/movie_loaded.cpp)

What it represents:
- complex GPU operator with async loading
- custom-ref output using a shared media session token
- operator-owned media session publication and clock updates
- use of the shared-handle service from GPU context

#### Shared-session GPU consumer

- [MovieVideoOut](/Users/jeff/Developer/vivid/operators/gpu/movie_video_out/movie_video_out.cpp)

What it represents:
- strict custom-ref input consumption
- custom handle resolution in GPU context
- operator-owned frame blit of session-owned video payloads

### Shared operator-layer support modules

#### Shared media-session contract

- [media_session.h](/Users/jeff/Developer/vivid/operators/shared/media_session/media_session.h)

What it represents:
- operator-layer shared state contract behind the generic runtime handle registry
- transport queue, audio ring, video payload queue, and sync-related shared counters
- a cross-domain pattern used by multiple movie operators

#### Movie decode/audio helper layer

- [video_decoder.h](/Users/jeff/Developer/vivid/operators/shared/movie_decode/video_decoder.h)
- [decoder_factory.h](/Users/jeff/Developer/vivid/operators/shared/movie_decode/decoder_factory.h)
- `operators/shared/movie_audio/*`

What it represents:
- local subsystem modules supporting the movie operators rather than standalone operators themselves
- split between decoding, texture upload, placeholder generation, and extractor logic

## Reference Operator Shortlist

These are the most useful current reference points for later review or future authoring work.

### Best references for basic authoring shape

- [LFO](/Users/jeff/Developer/vivid/operators/control/lfo/lfo.h)
- [Oscillator](/Users/jeff/Developer/vivid/operators/audio/oscillator/oscillator.cpp)
- [TextureLoader](/Users/jeff/Developer/vivid/operators/gpu/texture_loader/texture_loader.cpp)

### Best references for semantic metadata / inspector shaping

- [Smooth](/Users/jeff/Developer/vivid/operators/control/smooth/smooth.h)
- [Filter](/Users/jeff/Developer/vivid/operators/audio/filter/filter.cpp)

### Best references for composition and reuse

- [ModulatedGain](/Users/jeff/Developer/vivid/operators/control/modulated_gain/modulated_gain.cpp)
- [ChildOp](/Users/jeff/Developer/vivid/src/operator_api/child_op.h)
- [WgslFilterBase](/Users/jeff/Developer/vivid/src/operator_api/wgsl_filter.h)

### Best references for strings and typed custom ports

- [FolderList](/Users/jeff/Developer/vivid/operators/control/folder_list/folder_list.cpp)
- [StringSelect](/Users/jeff/Developer/vivid/operators/control/string_select/string_select.cpp)
- [MidiInput](/Users/jeff/Developer/vivid/operators/control/midi_input/midi_input.cpp)

### Best references for shared-handle / session-backed operators

- [MovieLoaded](/Users/jeff/Developer/vivid/operators/gpu/movie_loaded/movie_loaded.cpp)
- [MovieVideoOut](/Users/jeff/Developer/vivid/operators/gpu/movie_video_out/movie_video_out.cpp)
- [MovieAudioOut](/Users/jeff/Developer/vivid/operators/audio/movie_audio_out/movie_audio_out.cpp)
- [media_session.h](/Users/jeff/Developer/vivid/operators/shared/media_session/media_session.h)

## Observed Operator-API Themes

Without turning these into findings, the current operator contract appears to emphasize:

- code-first declaration of params and ports
- direct semantic annotation in operator source
- runtime-managed contexts rather than operator-managed wiring
- a small number of alternative authoring patterns layered on top of the same ABI
- custom port types as an important current extension point
- shared-handle-backed resource/session patterns for cross-domain complex data

The seed set also suggests that the project now has several “advanced canonical” patterns beyond basic audio/control/GPU operators:

- string-first dataflow
- custom typed port flow
- operator composition with `ChildOp`
- data-driven WGSL filter authoring
- shared media session publication/consumption

## Open Questions for Phase 4+

1. Which of these reference operators are intentionally canonical examples for users and scaffolding, versus just the current implementation of a feature?
2. How consistently do seed operators follow the same declaration and metadata conventions across domains?
3. How much of the operator authoring contract is stabilized, and how much is still evolving alongside runtime features like custom ports and shared handles?
4. Which advanced patterns should later review treat as first-class architecture, and which are specialized vertical slices?
5. How much do UI, scaffolding, and MCP/control-server tooling actually reflect the full operator contract visible in `src/operator_api/`?

## Phase Boundary

Phase 3 is complete when:

- the operator authoring contract is clear at a high level
- representative seed operators have been identified by pattern
- later review has a shortlist of reference operators to inspect deeply
- future authoring work has a practical map of which files exemplify which current patterns

This note intentionally stops short of operator-by-operator correctness review.
