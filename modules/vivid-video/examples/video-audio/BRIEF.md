# Vision

A video playback system demonstrating two approaches to audio: synchronized native playback for clean presentation, or chain-routed audio for applying real-time effects like delay and reverb.

## Aesthetic Goals
- Clean video playback with optional audio processing
- Spacious, atmospheric audio when effects are enabled
- Intuitive UI for adjusting delay, reverb, and gain in real-time

## Techniques Demonstrated
- VideoPlayer with internal vs. chain audio modes
- VideoAudio operator for extracting audio from video
- Audio effects chain: Delay, Reverb, AudioGain
- ImGui integration for live parameter control
