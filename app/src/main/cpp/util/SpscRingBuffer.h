// SpscRingBuffer.h
//
// The spec calls for "lock-free queues where possible" between the
// engine's threads, specifically so the render thread never blocks
// waiting on a mutex the decoder thread is holding (or vice versa) --
// a stall on either thread under a mutex is a dropped/duplicated frame,
// exactly the defect class the spec's seeking requirements ("No flashing.
// No duplicated frames. No stale frames.") are guarding against.
//
// This is intentionally a single-producer/single-consumer ring buffer, not
// a general MPMC lock-free queue: every hop in this engine
// (DecoderThread -> RenderThread, RenderThread -> EncoderThread) is
// exactly one writer and one reader. SPSC is a much simpler, well-known
// correct construction (a full general lock-free MPMC queue is a research
// topic; an incorrect hand-rolled one is a worse bug than a mutex would
// have been), and it's sufficient for every queue this engine needs.
//
// Capacity must be a power of two so the wrap-around index math is a mask,
// not a modulo -- modulo by a non-power-of-two is measurably slower when
// this runs on every decoded/rendered frame.

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <vector>

namespace nle {

template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacityPow2) : buffer_(capacityPow2), mask_(capacityPow2 - 1) {
        assert((capacityPow2 & (capacityPow2 - 1)) == 0 && "capacity must be a power of two");
    }

    // Producer-only. Returns false if the buffer is full -- callers (e.g.
    // DecoderThread) should treat that as "drop this frame" per the spec's
    // "Frame dropping" requirement, not as an error to retry-block on.
    bool TryPush(T item) {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t nextHead = (head + 1) & mask_;
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            return false;  // full
        }
        buffer_[head] = std::move(item);
        head_.store(nextHead, std::memory_order_release);
        return true;
    }

    // Consumer-only. Returns false if empty.
    bool TryPop(T& out) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = std::move(buffer_[tail]);
        tail_.store((tail + 1) & mask_, std::memory_order_release);
        return true;
    }

    // Consumer-only, non-destructive lookahead at the next item, used by
    // FrameQueue to peek a frame's timestamp before deciding whether to
    // consume it (see decode/FrameQueue.h).
    const T* Peek() const {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) return nullptr;
        return &buffer_[tail];
    }

    size_t Capacity() const { return buffer_.size(); }

    void Clear() {
        // Only safe to call when producer and consumer are both quiesced
        // (e.g. during a seek flush) -- see DecoderThread::Flush().
        tail_.store(head_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

private:
    std::vector<T> buffer_;
    size_t mask_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
};

}  // namespace nle
