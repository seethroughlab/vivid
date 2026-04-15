#pragma once

#include "decoded_frame_queue.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_set>

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
    // Pending work is bounded; duplicate nonzero request keys are ignored.
    using WorkFunction = std::function<DecodedFrame()>;
    bool submit_work(WorkFunction&& work,
                     uint64_t loop_generation = 0,
                     uint64_t request_key = 0);

    // Submit a pre-decoded frame directly into the ready queue.
    bool submit_decoded(DecodedFrame&& frame);

    // Pop the latest ready frame (called from the frame thread).
    bool pop_latest(DecodedFrame& out);
    bool pop_best(double target_pts,
                  uint64_t loop_generation,
                  double frame_duration,
                  DecodedFrame& out);
    bool has_ready_generation(uint64_t loop_generation) const;

    // Discard all pending work and queued frames.
    void flush();

    Generation generation() const;

private:
    void worker_loop();

    static constexpr size_t kMaxPendingWork = 12;

    struct WorkItem {
        WorkFunction work;
        Generation generation = 0;
        uint64_t loop_generation = 0;
        uint64_t request_key = 0;
    };

    std::thread thread_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool started_ = false;

    std::deque<WorkItem> pending_work_;
    std::unordered_set<uint64_t> pending_keys_;

    // Output queue (worker → frame thread).
    DecodedFrameQueue ready_queue_;
    std::atomic<Generation> generation_{0};
};
