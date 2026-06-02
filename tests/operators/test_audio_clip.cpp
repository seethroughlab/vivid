#include "audio_clip.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "test_helpers.h"

namespace {

void w16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void w32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

std::filesystem::path write_wav(const std::filesystem::path& path,
                                uint32_t sample_rate,
                                const std::vector<float>& samples) {
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint16_t block_align = channels * bits / 8;
    const uint32_t byte_rate = sample_rate * block_align;
    const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * block_align);

    std::vector<uint8_t> wav;
    wav.insert(wav.end(), {'R', 'I', 'F', 'F'});
    w32(wav, 36u + data_bytes);
    wav.insert(wav.end(), {'W', 'A', 'V', 'E'});
    wav.insert(wav.end(), {'f', 'm', 't', ' '});
    w32(wav, 16);
    w16(wav, 1);
    w16(wav, channels);
    w32(wav, sample_rate);
    w32(wav, byte_rate);
    w16(wav, block_align);
    w16(wav, bits);
    wav.insert(wav.end(), {'d', 'a', 't', 'a'});
    w32(wav, data_bytes);
    for (float f : samples) {
        const float c = std::max(-1.0f, std::min(1.0f, f));
        const int16_t s = static_cast<int16_t>(std::lrint(c * 32767.0f));
        w16(wav, static_cast<uint16_t>(s));
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
    return path;
}

struct AudioHarness {
    static constexpr uint32_t N = 16;
    AudioClip op;
    std::vector<float> play = std::vector<float>(N, 0.0f);
    std::vector<float> stop = std::vector<float>(N, 0.0f);
    std::vector<float> beat = std::vector<float>(N, 0.0f);
    std::vector<float> slice = std::vector<float>(N, 0.0f);
    std::vector<float> audio = std::vector<float>(N * 2, 0.0f);
    std::vector<float> pos = std::vector<float>(N, 0.0f);
    std::vector<float> done = std::vector<float>(N, 0.0f);
    std::vector<float> pending = std::vector<float>(N, 0.0f);
    std::vector<float> slice_count = std::vector<float>(N, 0.0f);
    std::vector<float> active_slice = std::vector<float>(N, -1.0f);
    float* inputs[4] = {play.data(), stop.data(), beat.data(), slice.data()};
    float* outputs[6] = {audio.data(), pos.data(), done.data(),
                         pending.data(), slice_count.data(), active_slice.data()};
    VividAudioContext ctx{};

    AudioHarness() {
        ctx.input_buffers = inputs;
        ctx.output_buffers = outputs;
        ctx.buffer_size = N;
        ctx.sample_rate = 48000;
        ctx.metronome_bpm = 120.0f;
        ctx.metronome_beats_per_bar = 4;
    }

    void clear_buffers() {
        std::fill(play.begin(), play.end(), 0.0f);
        std::fill(stop.begin(), stop.end(), 0.0f);
        std::fill(beat.begin(), beat.end(), 0.0f);
        std::fill(slice.begin(), slice.end(), 0.0f);
        std::fill(audio.begin(), audio.end(), 0.0f);
        std::fill(pos.begin(), pos.end(), 0.0f);
        std::fill(done.begin(), done.end(), 0.0f);
        std::fill(pending.begin(), pending.end(), 0.0f);
        std::fill(slice_count.begin(), slice_count.end(), 0.0f);
        std::fill(active_slice.begin(), active_slice.end(), -1.0f);
    }

    void load(const std::filesystem::path& path) {
        op.process_audio(&ctx); // publishes known sample rate
        op.file.str_value = path.string();
        op.main_thread_update(0.0);
        clear_buffers();
    }
};

bool all_done_zero_except_last(const std::vector<float>& done) {
    for (size_t i = 0; i + 1 < done.size(); ++i)
        if (done[i] != 0.0f) return false;
    return true;
}

float audio_energy(const std::vector<float>& audio) {
    float sum = 0.0f;
    for (float s : audio) sum += std::fabs(s);
    return sum;
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: AudioClip audio behavior ===\n\n");

    const auto dir = std::filesystem::temp_directory_path();

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_ramp_48k.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.loop.value = 1.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeExternal);
        for (uint32_t i = 0; i < h.N; ++i)
            h.beat[i] = static_cast<float>(i) / static_cast<float>(h.N - 1);

        h.op.process_audio(&h.ctx);
        check(std::fabs(h.pos.front() - 0.0f) < 1e-6f, "external position starts at beat phase");
        check(std::fabs(h.pos.back() - 1.0f) < 1e-6f, "external position ends at beat phase");
        check(h.pos[8] > h.pos[7], "external position is per-sample, not block-only");
        check(all_done_zero_except_last(h.done) && h.done.back() == 0.0f,
              "external mode emits no done pulse");
    }

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_reverse_external.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.loop.value = 1.0f;
        h.op.reverse.value = 1.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeExternal);
        for (uint32_t i = 0; i < h.N; ++i)
            h.beat[i] = static_cast<float>(i) / static_cast<float>(h.N - 1);

        h.op.process_audio(&h.ctx);
        check(h.audio.front() > 0.90f, "reverse external phase 0 reads the clip tail");
        check(h.audio[h.N - 1] < 0.05f, "reverse external phase 1 reads the clip head without underflow");
        check(std::fabs(h.pos.front() - 1.0f) < 1e-6f, "reverse external position starts at normalized tail");
        check(std::fabs(h.pos.back() - 0.0f) < 1e-6f, "reverse external position ends at normalized head");
    }

    {
        std::vector<float> samples(64, 0.25f);
        const auto path = write_wav(dir / "vivid_audio_clip_block_position.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.loop.value = 1.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeInternal);
        h.op.process_audio(&h.ctx);
        check(std::fabs(h.pos.front()) < 1e-6f, "free mode position starts at current playhead");
        check(h.pos[1] > h.pos[0], "free mode position advances per sample");
        check(h.pos.back() > h.pos.front(), "free mode position buffer is not final-sample-only");
    }

    {
        std::vector<float> samples(8, 0.5f);
        const auto path = write_wav(dir / "vivid_audio_clip_done.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.loop.value = 0.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeInternal);
        h.op.process_audio(&h.ctx);
        check(all_done_zero_except_last(h.done), "done pulse is isolated to final sample");
        check(h.done.back() == 1.0f, "non-looping completion emits done pulse");
    }

    {
        std::vector<float> samples(441, 0.1f);
        const auto path = write_wav(dir / "vivid_audio_clip_44k.wav", 44100, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.loop.value = 1.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeInternal);
        h.op.process_audio(&h.ctx);
        check(h.pos.back() > 0.02f && h.pos.back() < 0.04f,
              "44.1k source is resampled into the 48k runtime domain");
    }

    {
        std::vector<float> samples(64, 0.0f);
        samples[0] = 1.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_stretch_impulse.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 1.0f;
        h.op.loop.value = 0.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeInternal);

        float total_energy = 0.0f;
        bool saw_done = false;
        for (int block = 0; block < 64 && !saw_done; ++block) {
            h.clear_buffers();
            h.op.process_audio(&h.ctx);
            total_energy += audio_energy(h.audio);
            saw_done = h.done.back() == 1.0f;
        }
        check(total_energy > 0.01f, "stretch playback does not skip the leading transient");
        check(saw_done, "stretch non-looping playback eventually emits done");
    }

    {
        std::vector<float> samples(4, 0.5f);
        const auto path = write_wav(dir / "vivid_audio_clip_stretch_tail.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 1.0f;
        h.op.loop.value = 0.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeInternal);

        h.op.process_audio(&h.ctx);
        const bool first_block_done = h.done.back() == 1.0f;
        bool later_done = false;
        for (int block = 0; block < 64 && !later_done; ++block) {
            h.clear_buffers();
            h.op.process_audio(&h.ctx);
            later_done = h.done.back() == 1.0f;
        }
        check(!first_block_done, "stretch done waits for tail drain instead of firing on source end");
        check(later_done, "stretch tail drain eventually emits done");
    }

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_reverse.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.loop.value = 0.0f;
        h.op.reverse.value = 1.0f;
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(h.audio.front() > h.audio[15], "reverse playback reads source backwards");
        check(std::fabs(h.pos.front()) < 1e-6f, "reverse position starts at slice/clip head");
        check(h.pos.back() > h.pos.front(), "reverse position advances in normalized play direction");
    }

    {
        std::vector<float> samples(64, 0.25f);
        const auto path = write_wav(dir / "vivid_audio_clip_quantized_launch.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.launch_quantize.value = 1.0f;
        h.ctx.metronome_beats_elapsed = 1.25;
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(h.pending.back() == 1.0f, "quantized launch reports pending before boundary");
        check(audio_energy(h.audio) == 0.0f, "quantized launch stays silent before boundary");

        h.clear_buffers();
        h.ctx.metronome_beats_elapsed = 2.0;
        h.op.process_audio(&h.ctx);
        check(h.pending.back() == 0.0f, "quantized launch clears pending at boundary");
        check(audio_energy(h.audio) > 0.0f, "quantized launch starts at boundary");
    }

    {
        std::vector<float> samples(64, 0.25f);
        const auto path = write_wav(dir / "vivid_audio_clip_quantized_stop.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.launch_quantize.value = 1.0f;
        h.ctx.metronome_beats_elapsed = 1.25;
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(h.pending.back() == 1.0f, "pending launch exists before stop");

        h.clear_buffers();
        h.ctx.metronome_beats_elapsed = 1.5;
        h.stop[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(h.pending.back() == 0.0f, "stop cancels a pending quantized launch");

        h.clear_buffers();
        h.ctx.metronome_beats_elapsed = 2.0;
        h.op.process_audio(&h.ctx);
        check(audio_energy(h.audio) == 0.0f, "cancelled quantized launch does not start at boundary");
    }

    {
        std::vector<float> samples(64, 0.4f);
        const auto path = write_wav(dir / "vivid_audio_clip_launch_gate.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.launch_mode.value = 1.0f;
        std::fill(h.play.begin(), h.play.end(), 1.0f);
        h.op.process_audio(&h.ctx);
        check(audio_energy(h.audio) > 0.0f, "gate launch plays while play is held");

        h.clear_buffers();
        h.op.process_audio(&h.ctx);
        check(audio_energy(h.audio) == 0.0f, "gate launch stops when play is released");
    }

    {
        std::vector<float> samples(64, 0.4f);
        const auto path = write_wav(dir / "vivid_audio_clip_launch_toggle.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.launch_mode.value = 2.0f;
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(audio_energy(h.audio) > 0.0f, "toggle launch starts on first play edge");

        h.clear_buffers();
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(audio_energy(h.audio) == 0.0f, "toggle launch stops on second play edge");
    }

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_launch_repeat.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.launch_mode.value = 3.0f;
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);

        h.clear_buffers();
        h.op.process_audio(&h.ctx);
        check(h.audio.front() > 0.20f, "repeat setup advances before retrigger");

        h.clear_buffers();
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(h.audio.front() < 0.05f, "repeat launch retriggers from the start");
    }

    {
        std::vector<float> samples(64, 1.0f);
        const auto path = write_wav(dir / "vivid_audio_clip_fade.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.fade_in_ms.value = 1.0f;
        h.op.process_audio(&h.ctx);
        check(std::fabs(h.audio.front()) < 1e-6f, "fade in starts silent");
        check(h.audio[15] > h.audio.front(), "fade in rises through the block");
    }

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_slice.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.op.slice_mode.value = 3.0f;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.slice[0] = 2.0f;
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(h.slice_count.back() == 16.0f, "even16 slice mode reports slice count");
        check(h.active_slice.back() == 2.0f, "slice mode reports active slice");
        check(h.audio.front() > 0.10f && h.audio.front() < 0.20f,
              "slice mode starts playback at selected slice");
    }

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_manual_slice.wav", 48000, samples);

        AudioHarness h;
        h.op.auto_play.value = 0.0f;
        h.op.slice_mode.value = 2.0f;
        h.op.slice_points.str_value = "[16,32]";
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.slice_index.value = 1.0f;
        h.play[0] = 1.0f;
        h.op.process_audio(&h.ctx);
        check(h.slice_count.back() == 3.0f, "manual slice mode reports compiled slice count");
        check(h.active_slice.back() == 1.0f, "manual slice mode can use hidden slice_index fallback");
        check(h.audio.front() > 0.20f && h.audio.front() < 0.35f,
              "manual slice mode starts at selected authored boundary");
    }

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_warp.wav", 48000, samples);

        AudioHarness h;
        h.op.warp_points.str_value =
            R"([{"source_sample":0,"beat":0.0},{"source_sample":32,"beat":2.0},{"source_sample":64,"beat":4.0}])";
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.warp_enabled.value = 1.0f;
        h.op.file_bpm.value = 120.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeMetronome);
        h.ctx.metronome_beats_elapsed = 1.5;
        h.op.process_audio(&h.ctx);
        check(h.audio.front() > 0.30f && h.audio.front() < 0.45f,
              "warp-enabled metronome playback maps beat to source sample");
    }

    {
        std::vector<float> samples(64, 0.0f);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<float>(i) / 64.0f;
        const auto path = write_wav(dir / "vivid_audio_clip_warp_no_bpm.wav", 48000, samples);

        AudioHarness h;
        h.op.warp_points.str_value =
            R"([{"source_sample":0,"beat":0.0},{"source_sample":32,"beat":2.0},{"source_sample":64,"beat":4.0}])";
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.warp_enabled.value = 1.0f;
        h.op.file_bpm.value = 0.0f;
        h.op.clock_mode.value = static_cast<float>(vivid::kClockModeMetronome);
        h.ctx.metronome_beats_elapsed = 1.5;
        h.op.process_audio(&h.ctx);
        check(h.audio.front() > 0.30f && h.audio.front() < 0.45f,
              "authored warp metronome playback works without file_bpm in repitch path");
    }

    return 0;
}
