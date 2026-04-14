# Operator Inventory

Generated: 2026-04-13 | Commit: a30f7196
C++ Registered: 109 | WGSL Filters: 29 | Total: 138

## Audio Operators (37)

| Registered Name | Source | Env Dep | Used In (count) | Graphs |
| --- | --- | --- | --- | --- |
| AudioAnalysis | operators/audio/audio_analysis/audio_analysis.cpp | -- | 1 | parity_audio_first |
| Bitcrush | operators/audio/bitcrush/bitcrush.cpp | -- | 1 | chromatic_ghosts |
| Chorus | operators/audio/chorus/chorus.cpp | -- | 2 | chorus_demo, chorus_metronome_demo |
| Compressor | operators/audio/compressor/compressor.cpp | -- | 1 | compressor_demo |
| ConvolutionReverb | operators/audio/convolution_reverb/convolution_reverb.cpp | ir_files | 4 | convolution_reverb_mix_bus_fixture, convolution_reverb_pad_fixture, convolution_reverb_percussion_fixture, convolution_reverb_vocal_fixture |
| Delay | operators/audio/delay/delay.cpp | -- | 1 | chromatic_ghosts |
| Distortion | operators/audio/distortion/distortion.cpp | -- | 0 | -- |
| DrumClap | operators/audio/drum_clap/drum_clap.cpp | -- | 2 | drum_stack_demo, four_on_the_floor |
| DrumCymbal | operators/audio/drum_cymbal/drum_cymbal.cpp | -- | 1 | drum_stack_demo |
| DrumHiHat | operators/audio/drum_hihat/drum_hihat.cpp | -- | 5 | convolution_reverb_percussion_fixture, drum_stack_demo, four_on_the_floor, state_machine_demo, showcase_demo |
| DrumKick | operators/audio/drum_kick/drum_kick.cpp | -- | 6 | av_sync_demo, convolution_reverb_percussion_fixture, drum_stack_demo, four_on_the_floor, state_machine_demo (+1 more) |
| DrumSnare | operators/audio/drum_snare/drum_snare.cpp | -- | 5 | convolution_reverb_percussion_fixture, drum_stack_demo, four_on_the_floor, state_machine_demo, showcase_demo |
| DrumTom | operators/audio/drum_tom/drum_tom.cpp | -- | 1 | drum_stack_demo |
| DualFilter | operators/audio/dual_filter/dual_filter.cpp | -- | 0 | -- |
| Filter | operators/audio/filter/filter.cpp | -- | 13 | chromatic_ghosts, convolution_reverb_mix_bus_fixture, convolution_reverb_pad_fixture, convolution_reverb_vocal_fixture, filter_sweep (+8 more) |
| Flanger | operators/audio/flanger/flanger.cpp | -- | 1 | flanger_demo |
| FmSynth | operators/audio/fm_synth/fm_synth.cpp | -- | 4 | arpeggiator_demo, arpeggiator_metronome_demo, fm_synth_demo, state_machine_demo |
| Gain | operators/audio/gain/gain.cpp | -- | 37 | chorus_demo, chorus_metronome_demo, chromatic_ghosts, compressor_demo, convolution_reverb_vocal_fixture (+32 more) |
| GranularSynth | operators/audio/granular_synth/granular_synth.cpp | -- | 1 | granular_synth_demo |
| Limiter | operators/audio/limiter/limiter.cpp | -- | 1 | limiter_demo |
| MicInput | operators/audio/mic_input/mic_input.cpp | microphone | 0 | -- |
| MidiFilePlayer | operators/audio/midi_file_player/midi_file_player.cpp | midi_files | 0 | -- |
| Mixer | operators/audio/mixer/mixer.cpp | -- | 8 | chromatic_ghosts, compressor_demo, convolution_reverb_mix_bus_fixture, envelope_shapes, full_synth_patch (+3 more) |
| MovieFileAudio | operators/audio/movie_file_audio/movie_file_audio.cpp | movie_media | 15 | color_space_demo, crt_effect_demo, edge_demo, hsv_demo, lut_apply_demo (+10 more) |
| Noise | operators/audio/noise/noise.cpp | -- | 12 | chromatic_ghosts, compressor_demo, convolution_reverb_mix_bus_fixture, convolution_reverb_vocal_fixture, filter_sweep (+7 more) |
| Oscillator | operators/audio/oscillator/oscillator.cpp | -- | 33 | chorus_demo, chorus_metronome_demo, chromatic_ghosts, compressor_demo, convolution_reverb_mix_bus_fixture (+28 more) |
| ParametricEQ | operators/audio/parametric_eq/parametric_eq.cpp | -- | 1 | parametric_eq_demo |
| Phaser | operators/audio/phaser/phaser.cpp | -- | 1 | phaser_demo |
| PingPongDelay | operators/audio/ping_pong_delay/ping_pong_delay.cpp | -- | 1 | ping_pong_delay_demo |
| Reverb | operators/audio/reverb/reverb.cpp | -- | 4 | arpeggiator_demo, arpeggiator_metronome_demo, sequencer_demo, mfi_av_sync_demo |
| RingMod | operators/audio/ring_mod/ring_mod.cpp | -- | 1 | ring_mod_demo |
| SP404 | operators/audio/sp404/sp404.cpp | -- | 1 | sp404_demo |
| Sampler | operators/audio/sampler/sampler.cpp | sample_files | 1 | sampler_chromatic_demo |
| Slicer | operators/audio/slicer/slicer.cpp | -- | 1 | slicer_demo |
| SpectralFreeze | operators/audio/spectral_freeze/spectral_freeze.cpp | -- | 1 | spectral_freeze_demo |
| StereoPanWidth | operators/audio/stereo_pan_width/stereo_pan_width.cpp | -- | 2 | stereo_pan_width_demo, stereo_demo |
| Vocoder | operators/audio/vocoder/vocoder.cpp | -- | 1 | vocoder_demo |

## Control Operators (46)

| Registered Name | Source | Env Dep | Used In (count) | Graphs |
| --- | --- | --- | --- | --- |
| Alternate | operators/control/alternate/alternate.cpp | -- | 0 | -- |
| Arpeggiator | operators/control/arpeggiator/arpeggiator_au.cpp | -- | 3 | arpeggiator_demo, arpeggiator_metronome_demo, sampler_chromatic_demo |
| Basename | operators/control/basename/basename.cpp | -- | 1 | mfi_space_cycle_sync_demo |
| ChordProgression | operators/control/chord_progression/chord_progression_au.cpp | -- | 4 | arpeggiator_demo, arpeggiator_metronome_demo, sampler_chromatic_demo, slicer_demo |
| Clock | operators/control/clock/clock_au.cpp | -- | 19 | arpeggiator_demo, av_sync_demo, chromatic_ghosts, compressor_demo, convolution_reverb_percussion_fixture (+14 more) |
| ClockFr | operators/control/clock/clock_fr.cpp | -- | 4 | av_demo, demo, osc_av_loopback_demo, parity_cross_domain |
| DrumKit | operators/control/drum_kit/drum_kit_au.cpp | -- | 5 | convolution_reverb_percussion_fixture, drum_stack_demo, four_on_the_floor, state_machine_demo, showcase_demo |
| DrumSequencer | operators/control/drum_sequencer/drum_sequencer_au.cpp | -- | 6 | convolution_reverb_percussion_fixture, drum_stack_demo, four_on_the_floor, sp404_demo, state_machine_demo (+1 more) |
| EnvelopeAudio | operators/control/envelope/envelope_au.cpp | -- | 0 | -- |
| EnvelopeFr | operators/control/envelope/envelope_fr.cpp | -- | 1 | parity_cross_domain |
| Euclidean | operators/control/euclidean/euclidean_au.cpp | -- | 1 | chromatic_ghosts |
| FFTAnalysis | operators/control/fft_analysis/fft_analysis.cpp | -- | 0 | -- |
| FolderList | operators/control/folder_list/folder_list.cpp | filesystem | 1 | mfi_space_cycle_sync_demo |
| GateAudio | operators/control/gate/gate_au.cpp | -- | 0 | -- |
| Keyboard | operators/control/keyboard/keyboard.cpp | -- | 1 | mfi_space_cycle_sync_demo |
| Lfo | operators/control/lfo/lfo_au.cpp | -- | 17 | chorus_demo, chromatic_ghosts, flanger_demo, fm_synth_demo, full_synth_patch (+12 more) |
| LfoFr | operators/control/lfo/lfo_fr.cpp | -- | 44 | chorus_metronome_demo, chromatic_ghosts, displace_demo, dither_demo, edge_demo (+39 more) |
| Logic | operators/control/logic/logic.cpp | -- | 0 | -- |
| Macro | operators/control/macro/macro.cpp | -- | 0 | -- |
| Math | operators/control/math/math.cpp | -- | 3 | spirograph_demo, voronoi_mosaic_demo, path_animate_av_demo |
| MidiInput | operators/control/midi_input/midi_input.cpp | midi_hardware | 1 | midi_demo |
| ModulatedGain | operators/control/modulated_gain/modulated_gain.cpp | -- | 0 | -- |
| Mouse | operators/control/mouse/mouse.cpp | -- | 0 | -- |
| MsegAudio | operators/control/mseg/mseg_au.cpp | -- | 0 | -- |
| NoteDuration | operators/control/note_duration/note_duration.cpp | -- | 0 | -- |
| NotePattern | operators/control/note_pattern/note_pattern_au.cpp | -- | 1 | state_machine_demo |
| OscIn | operators/control/osc_in/osc_in.cpp | osc_network | 2 | osc_av_in_demo, osc_av_loopback_demo |
| OscOut | operators/control/osc_out/osc_out.cpp | osc_network | 1 | osc_av_loopback_demo |
| PatTransform | operators/control/pat_transform/pat_transform.cpp | -- | 0 | -- |
| PathAnimate | operators/control/path_animate/path_animate.cpp | -- | 0 | -- |
| PatternSeq | operators/control/pattern_seq/pattern_seq_au.cpp | -- | 0 | -- |
| PhaseToMidi | operators/control/phase_to_midi/phase_to_midi_au.cpp | -- | 0 | -- |
| Quantizer | operators/control/quantizer/quantizer_au.cpp | -- | 0 | -- |
| Repeat | operators/control/repeat/repeat.cpp | -- | 1 | lanes_intro_demo |
| SampleHoldAudio | operators/control/sample_hold/sample_hold_au.cpp | -- | 0 | -- |
| Select | operators/control/select/select.cpp | -- | 0 | -- |
| Sequencer | operators/control/sequencer/sequencer_au.cpp | -- | 1 | sequencer_demo |
| Smooth | operators/control/smooth/smooth_au.cpp | -- | 1 | full_synth_patch |
| SmoothFr | operators/control/smooth/smooth_fr.cpp | -- | 0 | -- |
| Stack | operators/control/stack/stack.cpp | -- | 1 | lanes_stack_demo |
| StateMachine | operators/control/state_machine/state_machine.cpp | -- | 1 | state_machine_demo |
| StepCounter | operators/control/step_counter/step_counter_au.cpp | -- | 0 | -- |
| StepCounterFr | operators/control/step_counter/step_counter_fr.cpp | -- | 1 | mfi_space_cycle_sync_demo |
| StringSelect | operators/control/string_select/string_select.cpp | -- | 1 | mfi_space_cycle_sync_demo |
| Tile | operators/control/tile/tile.cpp | -- | 0 | -- |
| Tracker | operators/control/tracker/tracker_au.cpp | -- | 0 | -- |

## Gpu Operators (26)

| Registered Name | Source | Env Dep | Used In (count) | Graphs |
| --- | --- | --- | --- | --- |
| Bloom | operators/gpu/bloom/bloom.cpp | -- | 15 | state_machine_demo, spirograph_demo, voronoi_cells_demo, voronoi_mosaic_demo, bloom_demo (+10 more) |
| CellularAutomata | operators/gpu/cellular_automata/cellular_automata.cpp | -- | 0 | -- |
| Composite | operators/gpu/composite/composite.cpp | -- | 11 | chromatic_ghosts, state_machine_demo, subtractive_drone, gradient_demo, composite_demo (+6 more) |
| Feedback | operators/gpu/feedback/feedback.cpp | -- | 8 | spirograph_demo, feedback_demo, nyan_trail_demo, path_animate_demo, star_spin_demo (+3 more) |
| Flocking | operators/gpu/flocking/flocking.cpp | -- | 0 | -- |
| Fluid | operators/gpu/fluid/fluid.cpp | -- | 0 | -- |
| InstancedShapes | operators/gpu/instanced_shapes/instanced_shapes.cpp | -- | 0 | -- |
| LutApply | operators/gpu/lut_apply/lut_apply.cpp | -- | 0 | -- |
| MeshWarp | operators/gpu/mesh_warp/mesh_warp.cpp | -- | 1 | mesh_warp_demo |
| Metaball | operators/gpu/metaball/metaball.cpp | -- | 2 | lanes_intro_demo, lanes_stack_demo |
| MetronomeViz | operators/gpu/metronome_viz/metronome_viz.cpp | -- | 1 | gpu_metronome_demo |
| MovieFileIn | operators/gpu/movie_file_in/movie_file_in.cpp | movie_media | 18 | color_space_demo, crt_effect_demo, edge_demo, hsv_demo, lut_apply_demo (+13 more) |
| Particles | operators/gpu/particles/particles.cpp | -- | 1 | particle_envelope_demo |
| ReactionDiffusion | operators/gpu/reaction_diffusion/reaction_diffusion.cpp | -- | 0 | -- |
| RichText | operators/gpu/rich_text/rich_text.cpp | -- | 0 | -- |
| Scopes | operators/gpu/scopes/scopes.cpp | -- | 1 | scopes_demo |
| Shape | operators/gpu/shape/shape.cpp | -- | 18 | av_sync_demo, state_machine_demo, displace_demo, switch_demo, transform_demo (+13 more) |
| SvgRender | operators/gpu/svg_render/svg_render.cpp | -- | 0 | -- |
| SyphonIn | operators/gpu/syphon_in/syphon_in.mm | syphon | 1 | syphon_in_demo |
| SyphonOut | operators/gpu/syphon_out/syphon_out.mm | syphon | 1 | syphon_out_demo |
| Text | operators/gpu/text/text.cpp | -- | 3 | chromatic_ghosts, subtractive_drone, mfi_space_cycle_sync_demo |
| TextureAnalysis | operators/gpu/texture_analysis/texture_analysis.cpp | -- | 2 | texture_analysis_demo, parity_visual_first |
| TextureLoader | operators/gpu/texture_loader/texture_loader.cpp | filesystem | 3 | voronoi_mosaic_demo, division_raster_demo, qbert_demo |
| TimeMachine | operators/gpu/time_machine/time_machine.cpp | -- | 1 | webcam_timemachine_demo |
| Trails | operators/gpu/trails/trails.cpp | -- | 0 | -- |
| WebcamIn | operators/gpu/webcam_in/webcam_in.cpp | camera | 1 | webcam_timemachine_demo |

## WGSL Filter Operators (29)

| Name | Source | Used In (count) | Graphs |
| --- | --- | --- | --- |
| Blur | filters/blur.wgsl | 2 | mfi_blur_demo, mfi_kitchen_sink_demo |
| ChromaticAberration | filters/chromatic_aberration.wgsl | 1 | wgsl_filters_demo |
| Color Space | filters/color_space.wgsl | 1 | color_space_demo |
| CRTEffect | filters/crt_effect.wgsl | 2 | crt_effect_demo, wgsl_filters_demo |
| Displace | filters/displace.wgsl | 3 | displace_demo, wgsl_filters_demo, mfi_displace_demo |
| Dither | filters/dither.wgsl | 1 | dither_demo |
| DivisionRaster | filters/division_raster.wgsl | 1 | division_raster_demo |
| Edge | filters/edge.wgsl | 2 | edge_demo, wgsl_filters_demo |
| Edge Blend | filters/edge_blend.wgsl | 1 | edge_blend_demo |
| Gradient | filters/gradient.wgsl | 6 | dither_demo, gradient_demo, switch_demo, wgsl_filters_demo, edge_blend_demo (+1 more) |
| Halftone | filters/halftone.wgsl | 2 | mfi_halftone_demo, mfi_kitchen_sink_demo |
| HexGrid | filters/hex_grid.wgsl | 1 | qbert_demo |
| HSV | filters/hsv.wgsl | 6 | chromatic_ghosts, subtractive_drone, hsv_demo, wgsl_filters_demo, nyan_trail_demo (+1 more) |
| Levels | filters/levels.wgsl | 4 | scanlines_demo, wgsl_filters_demo, mfi_kitchen_sink_demo, mfi_posterize_demo |
| Mirror | filters/mirror.wgsl | 2 | mirror_demo, wgsl_filters_demo |
| Pixelate | filters/pixelate.wgsl | 3 | chromatic_ghosts, subtractive_drone, wgsl_filters_demo |
| Posterize | filters/posterize.wgsl | 5 | chromatic_ghosts, subtractive_drone, wgsl_filters_demo, mfi_kitchen_sink_demo, mfi_posterize_demo |
| Quad Warp | filters/quad_warp.wgsl | 1 | quad_warp_demo |
| RadialRainbow | filters/radial_rainbow.wgsl | 1 | radial_rainbow_demo |
| Ramp | filters/ramp.wgsl | 1 | ramp_demo |
| RasterGrid | filters/raster_grid.wgsl | 0 | -- |
| Scanlines | filters/scanlines.wgsl | 4 | chromatic_ghosts, subtractive_drone, scanlines_demo, wgsl_filters_demo |
| SolidColor | filters/solid_color.wgsl | 1 | switch_demo |
| Spirograph | filters/spirograph.wgsl | 1 | spirograph_demo |
| Switch | filters/switch_op.wgsl | 1 | switch_demo |
| Test Pattern | filters/test_pattern.wgsl | 1 | test_pattern_demo |
| Tile | filters/tile.wgsl | 0 | -- |
| Transform | filters/transform.wgsl | 4 | transform_demo, wgsl_filters_demo, path_animate_av_demo, path_animate_demo |
| Voronoi | filters/voronoi.wgsl | 3 | voronoi_cells_demo, voronoi_mosaic_demo, voronoi_demo |

## Operators Not Used in Any Graph (36)

| Name | Domain/Type | Source |
| --- | --- | --- |
| Alternate | control | operators/control/alternate/alternate.cpp |
| CellularAutomata | gpu | operators/gpu/cellular_automata/cellular_automata.cpp |
| Distortion | audio | operators/audio/distortion/distortion.cpp |
| DualFilter | audio | operators/audio/dual_filter/dual_filter.cpp |
| EnvelopeAudio | control | operators/control/envelope/envelope_au.cpp |
| FFTAnalysis | control | operators/control/fft_analysis/fft_analysis.cpp |
| Flocking | gpu | operators/gpu/flocking/flocking.cpp |
| Fluid | gpu | operators/gpu/fluid/fluid.cpp |
| GateAudio | control | operators/control/gate/gate_au.cpp |
| InstancedShapes | gpu | operators/gpu/instanced_shapes/instanced_shapes.cpp |
| Logic | control | operators/control/logic/logic.cpp |
| LutApply | gpu | operators/gpu/lut_apply/lut_apply.cpp |
| Macro | control | operators/control/macro/macro.cpp |
| MicInput | audio | operators/audio/mic_input/mic_input.cpp |
| MidiFilePlayer | audio | operators/audio/midi_file_player/midi_file_player.cpp |
| ModulatedGain | control | operators/control/modulated_gain/modulated_gain.cpp |
| Mouse | control | operators/control/mouse/mouse.cpp |
| MsegAudio | control | operators/control/mseg/mseg_au.cpp |
| NoteDuration | control | operators/control/note_duration/note_duration.cpp |
| PatTransform | control | operators/control/pat_transform/pat_transform.cpp |
| PathAnimate | control | operators/control/path_animate/path_animate.cpp |
| PatternSeq | control | operators/control/pattern_seq/pattern_seq_au.cpp |
| PhaseToMidi | control | operators/control/phase_to_midi/phase_to_midi_au.cpp |
| Quantizer | control | operators/control/quantizer/quantizer_au.cpp |
| ReactionDiffusion | gpu | operators/gpu/reaction_diffusion/reaction_diffusion.cpp |
| RichText | gpu | operators/gpu/rich_text/rich_text.cpp |
| SampleHoldAudio | control | operators/control/sample_hold/sample_hold_au.cpp |
| Select | control | operators/control/select/select.cpp |
| SmoothFr | control | operators/control/smooth/smooth_fr.cpp |
| StepCounter | control | operators/control/step_counter/step_counter_au.cpp |
| SvgRender | gpu | operators/gpu/svg_render/svg_render.cpp |
| Tile | control | operators/control/tile/tile.cpp |
| Tracker | control | operators/control/tracker/tracker_au.cpp |
| Trails | gpu | operators/gpu/trails/trails.cpp |
| RasterGrid | wgsl_filter | filters/raster_grid.wgsl |
| Tile | wgsl_filter | filters/tile.wgsl |

## Environment-Dependent Operators

| Label | Operators |
| --- | --- |
| camera | WebcamIn |
| filesystem | FolderList, TextureLoader |
| ir_files | ConvolutionReverb |
| microphone | MicInput |
| midi_files | MidiFilePlayer |
| midi_hardware | MidiInput |
| movie_media | MovieFileAudio, MovieFileIn |
| osc_network | OscIn, OscOut |
| sample_files | Sampler |
| syphon | SyphonIn, SyphonOut |

