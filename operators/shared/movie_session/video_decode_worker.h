#pragma once

#include "decoded_frame_queue.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// Background worker that executes decode-copy work items and pushes results
// into a DecodedFrameQueue.  The work function is a generic callable so the
// movie_session dylib stays platform-agnostic (no AVFoundation dependency).
class VideoDecodeWorker {
public:
    using Generation = uint64_t;
    VideoDecodeWorker() = default;
    ~VideoDecodeWorker();

    void start();
    void stop();

    // Submit a copy function to execute on the worker thread.
    // If a previous work item hasn't been processed yet, it is replaced
    // and its result discarded (prevents CVPixelBuffer accumulation).
    using WorkFunction = std::function<DecodedFrame()>;
    void submit_work(WorkFunction&& work);

    // Submit a pre-decoded frame directly into the ready queue.
    void submit_decoded(DecodedFrame&& frame);

    // Pop the latest ready frame (called from the frame thread).
    bool pop_latest(DecodedFrame& out);

    // Discard all pending work and queued frames.
    void flush();

    Generation generation() const;

private:
    void worker_loop();

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool started_ = false;

    // Single pending work slot — newest wins.
    WorkFunction pending_work_;
    Generation pending_generation_ = 0;
    bool has_pending_ = false;

    // Output queue (worker → frame thread).
    DecodedFrameQueue ready_queue_;
    std::atomic<Generation> generation_{0};
};
