// DecoderThread.h
//
// The spec requires decoding to happen on its own dedicated thread with
// "Asynchronous decoding, Frame queue, Timestamp preservation, Frame
// dropping, Seeking, Decoder flush, Loop-safe behavior." This class is
// that thread. It owns the extractor-read / codec-feed / codec-drain loop
// and is the only thing that touches the AMediaExtractor/AMediaCodec
// pointers held by Decoder after construction -- Decoder's own methods
// (FrameNear, SeekTo) only ever post requests to this thread or read from
// the already-thread-safe FrameQueue, never call the extractor/codec
// directly from another thread. NDK MediaCodec instances are not safe to
// drive from multiple threads concurrently, so this single-owner rule is
// a correctness requirement, not just a style preference.

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "nle/core/Types.h"

struct AMediaExtractor;
struct AMediaCodec;
struct ASurfaceTexture;

namespace nle {

class Decoder;
class FrameQueue;

class DecoderThread {
public:
    explicit DecoderThread(Decoder* owner);
    ~DecoderThread();

    void Start();
    void Stop();

    // Non-blocking: records the request and wakes the thread. If a seek is
    // already in flight, the new request supersedes it -- "cancel old seek
    // requests" from the spec -- rather than queuing both, which is what
    // prevents fast scrubbing from backing up a queue of stale seeks.
    void RequestSeek(TimeUs timeUs);

private:
    void Run();
    void HandleSeekIfRequested();
    void DecodeOneInputChunk();
    void DrainOneOutputFrame();

    Decoder* owner_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::mutex seekMutex_;
    std::condition_variable seekCv_;
    std::atomic<bool> seekPending_{false};
    TimeUs pendingSeekTime_ = kTimeInvalid;
    // Bumped on every new seek request; DrainOneOutputFrame compares each
    // freshly decoded frame's "generation" against this to discard frames
    // that were in flight from a now-superseded seek, which is how stale
    // frames are prevented from ever reaching FrameQueue after a fast
    // scrub lands on a different target time.
    std::atomic<uint64_t> seekGeneration_{0};

    bool sawInputEOS_ = false;
    bool sawOutputEOS_ = false;
    bool loop_ = true;  // spec: "Loop-safe behavior" -- Phase 1 default is loop-on-EOS during preview
};

}  // namespace nle
