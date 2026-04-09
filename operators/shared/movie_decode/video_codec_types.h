#pragma once

// Video codec types shared between the decoder layer and the session layer.
// Kept in a standalone header to avoid circular dependencies between
// video_decoder.h and decoded_frame_queue.h.

enum class VideoFrameCompressionMode {
    UncompressedBGRA,
    CompressedBC
};

enum class VideoCompressedFormat {
    None,
    BC1,
    BC3,
    BC4
};
