# Environment Labels

Generated: 2026-04-13 | Commit: a30f7196

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
| ir_files | ConvolutionReverb | 4 | No |
| microphone | MicInput | 0 | No |
| midi_files | MidiFilePlayer | 0 | No |
| midi_hardware | MidiInput | 1 | No |
| movie_media | MovieFileAudio, MovieFileIn | 18 | No |
| osc_network | OscIn, OscOut | 2 | No |
| sample_files | Sampler | 1 | No |
| syphon | SyphonIn, SyphonOut | 2 | No |

## Graphs by Label

### camera (1 graphs)

- `gpu/webcam_timemachine_demo.json`

### filesystem (4 graphs)

- `filters/voronoi_mosaic_demo.json`
- `io/movie_file/mfi_space_cycle_sync_demo.json`
- `paperjs/division_raster_demo.json`
- `paperjs/qbert_demo.json`

### ir_files (4 graphs)

- `audio/convolution_reverb_mix_bus_fixture.json`
- `audio/convolution_reverb_pad_fixture.json`
- `audio/convolution_reverb_percussion_fixture.json`
- `audio/convolution_reverb_vocal_fixture.json`

### midi_hardware (1 graphs)

- `audio/midi_demo.json`

### movie_media (18 graphs)

- `filters/color_space_demo.json`
- `filters/crt_effect_demo.json`
- `filters/edge_demo.json`
- `filters/hsv_demo.json`
- `filters/lut_apply_demo.json`
- `filters/mirror_demo.json`
- `filters/scanlines_demo.json`
- `filters/scopes_demo.json`
- `filters/wgsl_filters_demo.json`
- `gpu/movie_loaded_demo.json`
- `io/movie_file/mfi_av_sync_demo.json`
- `io/movie_file/mfi_blur_demo.json`
- `io/movie_file/mfi_displace_demo.json`
- `io/movie_file/mfi_halftone_demo.json`
- `io/movie_file/mfi_kitchen_sink_demo.json`
- `io/movie_file/mfi_posterize_demo.json`
- `io/movie_file/mfi_space_cycle_sync_demo.json`
- `io/movie_file/mfi_video_only.json`

### osc_network (2 graphs)

- `io/osc_av_in_demo.json`
- `io/osc_av_loopback_demo.json`

### sample_files (1 graphs)

- `audio/sampler_chromatic_demo.json`

### syphon (2 graphs)

- `io/syphon_in_demo.json`
- `io/syphon_out_demo.json`

