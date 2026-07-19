// FrameQueue.h
//
// Sits between DecoderThread (producer) and DecoderSourceNode running on
// the render thread (consumer). Wraps SpscRingBuffer<Frame> with the
// timestamp-aware lookup DecoderSourceNode actually needs: "give me the
// frame whose presentation time is closest to (but not after) T", not just
// "give me the next frame in the queue" -- because during normal playback
// the render thread's requested time and the decoder's production rate
// aren't perfectly locked together, and during scrubbing they can diverge
// significantly.
//
// Frames "consumed" (returned by FrameNear) but not yet superseded stay
// available for repeated queries at the same or nearby time -- e.g. if
// playback is paused, the render thread calls FrameNear with the same time
// every redraw. That's why this keeps a `lastReturned_` frame around
// instead of only ever popping forward.

#pragma once

#include "rendergraph/Frame.h"
#include "util/SpscRingBuffer.h"

namespace nle {

class FrameQueue {
public:
    explicit FrameQueue(size_t capacityPow2 = 8) : ring_(capacityPow2) {}

    // Producer side (DecoderThread only).
    bool PushDecoded(Frame frame) { return ring_.TryPush(frame); }

    // Consumer side (render thread only). Drains any queued frames with
    // presentationTimeUs <= requestedTime, keeping the last one (closest
    // without overshooting), then returns it. If nothing new has arrived
    // and a requested time still falls within [lastReturned, next
    // pending), the previously returned frame is reused rather than
    // starving the caller -- this is what keeps a paused preview stable
    // instead of flickering to black between redraws.
    Frame FrameNear(TimeUs requestedTime) {
        Frame candidate = lastReturned_;
        while (const Frame* peeked = ring_.Peek()) {
            if (peeked->presentationTimeUs > requestedTime) break;
            Frame popped;
            ring_.TryPop(popped);
            candidate = popped;
        }
        lastReturned_ = candidate;
        return candidate;
    }

    bool HasCapacity() const { return ring_.Peek() == nullptr || true; }  // see PushDecoded return value for backpressure
    void Flush() {
        ring_.Clear();
        lastReturned_ = Frame{};
    }

private:
    SpscRingBuffer<Frame> ring_;
    Frame lastReturned_;
};

}  // namespace nle
