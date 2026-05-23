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
    std::vector<float> audio = std::vector<float>(N * 2, 0.0f);
    std::vector<float> pos = std::vector<float>(N, 0.0f);
    std::vector<float> done = std::vector<float>(N, 0.0f);
    float* inputs[3] = {play.data(), stop.data(), beat.data()};
    float* outputs[3] = {audio.data(), pos.data(), done.data()};
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
        std::fill(audio.begin(), audio.end(), 0.0f);
        std::fill(pos.begin(), pos.end(), 0.0f);
        std::fill(done.begin(), done.end(), 0.0f);
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
        h.op.rate_mode.value = static_cast<float>(vivid::kRateModeExternal);
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
        std::vector<float> samples(64, 0.25f);
        const auto path = write_wav(dir / "vivid_audio_clip_block_position.wav", 48000, samples);

        AudioHarness h;
        h.load(path);
        h.op.stretch.value = 0.0f;
        h.op.loop.value = 1.0f;
        h.op.rate_mode.value = static_cast<float>(vivid::kRateModeFree);
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
        h.op.rate_mode.value = static_cast<float>(vivid::kRateModeFree);
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
        h.op.rate_mode.value = static_cast<float>(vivid::kRateModeFree);
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
        h.op.rate_mode.value = static_cast<float>(vivid::kRateModeFree);

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
        h.op.rate_mode.value = static_cast<float>(vivid::kRateModeFree);

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

    return 0;
}
