#include <cstdio>
#include <cmath>

#include "operators/shared/media_session/media_session.h"

static int g_failures = 0;

static void expect_true(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++g_failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main() {
    vivid::media::MediaSession session;

    vivid::media::TransportCommand src{};
    src.type = vivid::media::TransportCommandType::SetSource;
    src.generation = 7;
    src.source_path = "/tmp/example.mov";
    src.speed = 1.0f;
    src.playing = 1;
    src.loop_enabled = 1;
    vivid::media::media_session_enqueue_command(session, src);

    vivid::media::TransportCommand seek{};
    seek.type = vivid::media::TransportCommandType::Seek;
    seek.generation = 7;
    seek.seek_time_s = 1.234;
    vivid::media::media_session_enqueue_command(session, seek);

    auto a = vivid::media::media_session_pop_command(session);
    expect_true(a.has_value(), "first command exists");
    expect_true(a && a->type == vivid::media::TransportCommandType::SetSource, "first command is SetSource");
    expect_true(a && a->source_path == "/tmp/example.mov", "SetSource path preserved");
    expect_true(a && a->generation == 7, "SetSource generation preserved");

    auto b = vivid::media::media_session_pop_command(session);
    expect_true(b.has_value(), "second command exists");
    expect_true(b && b->type == vivid::media::TransportCommandType::Seek, "second command is Seek");
    expect_true(b && b->generation == 7, "Seek generation preserved");
    expect_true(b && std::fabs(b->seek_time_s - 1.234) < 1e-6, "Seek payload preserved");

    auto c = vivid::media::media_session_pop_command(session);
    expect_true(!c.has_value(), "queue empty after pops");
    expect_true(session.transport_serial == 2, "transport serial increments with enqueue");
    vivid::media::media_session_note_generation_transition(session);
    vivid::media::media_session_note_generation_transition(session);
    expect_true(session.generation_transitions.load() == 2,
                "generation transition counter increments");

    vivid::media::VideoFramePayload vf{};
    vf.generation = 7;
    vf.frame_index = 42;
    vf.monotonic_time_s = 3.5;
    vf.width = 320;
    vf.height = 240;
    vf.format = 99;
    vf.compression_mode = vivid::media::VideoFrameCompressionMode::CompressedBC;
    vf.ycocg_encoded = 1;
    vf.bytes = {1, 2, 3, 4, 5};
    vivid::media::media_session_enqueue_video_frame(session, vf);
    expect_true(session.video_payload_enqueued.load() == 1, "video payload enqueue counter increments");

    auto vf_out = vivid::media::media_session_pop_video_frame(session);
    expect_true(vf_out.has_value(), "video frame payload exists");
    expect_true(vf_out && vf_out->generation == 7, "video payload generation preserved");
    expect_true(vf_out && vf_out->frame_index == 42, "video payload frame index preserved");
    expect_true(vf_out && vf_out->width == 320 && vf_out->height == 240, "video payload dimensions preserved");
    expect_true(vf_out && vf_out->format == 99, "video payload format preserved");
    expect_true(vf_out && vf_out->compression_mode == vivid::media::VideoFrameCompressionMode::CompressedBC,
                "video payload compression mode preserved");
    expect_true(vf_out && vf_out->ycocg_encoded == 1, "video payload ycocg flag preserved");
    expect_true(vf_out && vf_out->bytes.size() == 5 && vf_out->bytes[0] == 1 && vf_out->bytes[4] == 5,
                "video payload bytes preserved");

    auto vf_empty = vivid::media::media_session_pop_video_frame(session);
    expect_true(!vf_empty.has_value(), "video payload queue empty after pop");
    expect_true(session.video_payload_popped.load() == 1, "video payload pop counter increments");

    for (uint32_t i = 0; i < 8; ++i) {
        vivid::media::VideoFramePayload burst{};
        burst.frame_index = i;
        burst.bytes = {0, 1, 2, 3};
        vivid::media::media_session_enqueue_video_frame(session, std::move(burst));
    }
    expect_true(session.video_payload_dropped.load() > 0, "video payload drop counter increments on overflow");
    expect_true(session.video_payload_depth_high_water.load() >= 4, "video payload high-water depth recorded");

    vivid::media::media_session_audio_ring_clear(session);
    session.audio_ring_sample_rate.store(10.0f);
    session.audio_ring_speed.store(2.0f);
    session.audio_read_head_media_time.store(1.0);
    float write_l[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float write_r[4] = {1.1f, 1.2f, 1.3f, 1.4f};
    uint32_t wrote = vivid::media::media_session_audio_write(session, write_l, write_r, 4);
    expect_true(wrote == 4, "audio ring writes requested frames");
    expect_true(vivid::media::media_session_audio_available_read(session) == 4,
                "audio ring available_read updated");
    expect_true(session.audio_frames_written.load() == 4, "audio frames-written counter increments");
    expect_true(session.audio_ring_depth_high_water.load() >= 4, "audio ring high-water depth recorded");

    float out_l[4] = {};
    float out_r[4] = {};
    uint32_t read = vivid::media::media_session_audio_read(session, out_l, out_r, 3);
    expect_true(read == 3, "audio ring reads requested subset");
    expect_true(std::fabs(out_l[0] - 0.1f) < 1e-6f && std::fabs(out_l[2] - 0.3f) < 1e-6f,
                "audio ring left samples preserved");
    expect_true(std::fabs(out_r[0] - 1.1f) < 1e-6f && std::fabs(out_r[2] - 1.3f) < 1e-6f,
                "audio ring right samples preserved");
    expect_true(std::fabs(session.audio_read_head_media_time.load() - 1.6) < 1e-6,
                "audio ring read advances media-time by frames*speed/sample_rate");
    expect_true(session.audio_frames_read.load() == 3, "audio frames-read counter increments");

    uint32_t dropped = vivid::media::media_session_audio_discard(session, 1);
    expect_true(dropped == 1, "audio ring discards available frame");
    expect_true(vivid::media::media_session_audio_available_read(session) == 0,
                "audio ring empty after read+discard");
    expect_true(std::fabs(session.audio_read_head_media_time.load() - 1.8) < 1e-6,
                "audio ring discard advances media-time");
    expect_true(session.audio_frames_discarded.load() == 1, "audio frames-discarded counter increments");

    float short_l[4] = {};
    float short_r[4] = {};
    uint32_t short_read = vivid::media::media_session_audio_read(session, short_l, short_r, 4);
    expect_true(short_read == 0, "audio underrun read returns available frame count");
    expect_true(session.audio_underrun_callbacks.load() >= 1, "audio underrun callback counter increments");
    expect_true(session.audio_underrun_frames.load() >= 4, "audio underrun frame counter increments");

    float big_l[vivid::media::MediaSession::kAudioRingCapacity] = {};
    float big_r[vivid::media::MediaSession::kAudioRingCapacity] = {};
    uint32_t overflow_write = vivid::media::media_session_audio_write(
        session, big_l, big_r, vivid::media::MediaSession::kAudioRingCapacity);
    expect_true(overflow_write < vivid::media::MediaSession::kAudioRingCapacity,
                "audio write respects ring capacity");
    expect_true(session.audio_write_overflow_frames.load() > 0,
                "audio write overflow counter increments");

    if (g_failures != 0) {
        std::fprintf(stderr, "\n%d test(s) failed.\n", g_failures);
        return 1;
    }
    return 0;
}
