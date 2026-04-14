# Environment Labels

Generated: 2026-04-14 | Commit: 6eeba731

## Label Definitions

| Label | Meaning | Skip OK for Beta? |
| --- | --- | --- |
| camera | Requires webcam access | Yes, not in intro path |
| microphone | Requires microphone permission | Yes, not in intro path |
| midi_hardware | Requires MIDI controller or IAC driver | Yes |
| midi_files | Requires .mid files on disk | Yes |
| syphon | Requires Syphon sender/receiver (macOS) | Yes |
| movie_media | Requires movie file in media folder | Conditional -- must degrade gracefully |
| osc_network | Requires OSC sender/receiver app | Yes |
| ir_files | Requires impulse response audio files | Conditional |
| sample_files | Requires audio sample files on disk | Conditional |
| filesystem | Requires specific files/folders on disk | Conditional |

## Cross-Reference

| Label | Operators | Graph Count | In Intro Path? |
| --- | --- | --- | --- |
| camera | WebcamIn | 1 | No |
| filesystem | FolderList, TextureLoader | 4 | No |
| ir_files | ConvolutionReverb | 5 | No |
| microphone | MicInput | 0 | No |
| midi_files | MidiFilePlayer | 0 | No |
| midi_hardware | MidiInput | 1 | No |
| movie_media | MovieFileAudio, MovieFileIn | 18 | No |
| osc_network | OscIn, OscOut | 2 | No |
| sample_files | Sampler | 1 | No |
| syphon | SyphonIn, SyphonOut | 2 | No |

## Graphs by Label

### camera (1 graphs)

- `reference_graphs/gpu/webcam_timemachine_demo.json`

### filesystem (4 graphs)

- `reference_graphs/media/movie_file/mfi_space_cycle_sync_demo.json`
- `reference_graphs/media/texture_loader/division_raster_demo.json`
- `reference_graphs/media/texture_loader/qbert_demo.json`
- `reference_graphs/media/texture_loader/voronoi_mosaic_demo.json`

### ir_files (5 graphs)

- `graphs/audio/four_on_the_floor.json`
- `tests/graphs/listening/audio/convolution_reverb_mix_bus_fixture.json`
- `tests/graphs/listening/audio/convolution_reverb_pad_fixture.json`
- `tests/graphs/listening/audio/convolution_reverb_percussion_fixture.json`
- `tests/graphs/listening/audio/convolution_reverb_vocal_fixture.json`

### midi_hardware (1 graphs)

- `reference_graphs/io/midi_demo.json`

### movie_media (18 graphs)

- `graphs/filters/crt_effect_demo.json`
- `graphs/filters/edge_demo.json`
- `graphs/filters/mirror_demo.json`
- `graphs/filters/scanlines_demo.json`
- `graphs/filters/wgsl_filters_demo.json`
- `graphs/media/movie_file/mfi_av_sync_demo.json`
- `graphs/media/movie_file/mfi_video_only.json`
- `reference_graphs/filters/color_space_demo.json`
- `reference_graphs/filters/hsv_demo.json`
- `reference_graphs/filters/lut_apply_demo.json`
- `reference_graphs/filters/scopes_demo.json`
- `reference_graphs/media/movie_file/mfi_blur_demo.json`
- `reference_graphs/media/movie_file/mfi_displace_demo.json`
- `reference_graphs/media/movie_file/mfi_halftone_demo.json`
- `reference_graphs/media/movie_file/mfi_kitchen_sink_demo.json`
- `reference_graphs/media/movie_file/mfi_posterize_demo.json`
- `reference_graphs/media/movie_file/mfi_space_cycle_sync_demo.json`
- `reference_graphs/media/movie_file/movie_loaded_demo.json`

### osc_network (2 graphs)

- `graphs/io/osc_av_loopback_demo.json`
- `reference_graphs/io/osc_av_in_demo.json`

### sample_files (1 graphs)

- `graphs/audio/sampler_chromatic_demo.json`

### syphon (2 graphs)

- `reference_graphs/io/syphon_in_demo.json`
- `reference_graphs/io/syphon_out_demo.json`

