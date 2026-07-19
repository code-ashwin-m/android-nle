// PlaybackClock.h
//
// Every thread in the engine (decoder, render, encoder, UI-facing state
// queries) needs to agree on "what time is it right now" without locking
// against each other constantly. PlaybackClock is the one place that owns
// this: it's backed by std::atomic, not a mutex, because it's read far more
// often (every decode/render iteration) than it's written (once per
// play/pause/seek), and a lock-free read keeps the render thread from ever
// blocking on the UI or playback thread.
//
// Wall-clock-to-media-time mapping: when playing, current time is derived
// from (wallClockStartNs, mediaStartUs, rate) rather than incremented
// per-frame. Deriving it avoids drift accumulation -- incrementing by
// "1 frame" every callback slowly desyncs from real elapsed time under any
// scheduling jitter, which is exactly the kind of bug that shows up as
// "playback slowly falls out of audio sync" after a few minutes.

#pragma once

#include <atomic>
#include <chrono>

#include "nle/core/Types.h"

namespace nle {

enum class PlaybackState {
    Stopped,
    Playing,
    Paused,
    Seeking,
};

class PlaybackClock {
public:
    void Play(double rate = 1.0) {
        mediaStartUs_ = CurrentTimeUs();
        wallStartNs_ = NowNs();
        rate_.store(rate, std::memory_order_relaxed);
        state_.store(PlaybackState::Playing, std::memory_order_release);
    }

    void Pause() {
        mediaStartUs_ = CurrentTimeUs();
        state_.store(PlaybackState::Paused, std::memory_order_release);
    }

    // Seeking snaps the clock to an exact time immediately; PlaybackEngine
    // is responsible for holding PlaybackState::Seeking until the decoder
    // has actually produced the requested frame (see decode/Decoder.h).
    void SeekTo(TimeUs timeUs) {
        mediaStartUs_ = timeUs;
        wallStartNs_ = NowNs();
        state_.store(PlaybackState::Seeking, std::memory_order_release);
    }

    void MarkSeekComplete() {
        auto expected = PlaybackState::Seeking;
        state_.compare_exchange_strong(expected, PlaybackState::Paused);
    }

    void Stop() {
        mediaStartUs_ = 0;
        state_.store(PlaybackState::Stopped, std::memory_order_release);
    }

    PlaybackState State() const { return state_.load(std::memory_order_acquire); }
    double Rate() const { return rate_.load(std::memory_order_relaxed); }

    // The authoritative current media time. Safe to call from any thread.
    TimeUs CurrentTimeUs() const {
        if (state_.load(std::memory_order_acquire) != PlaybackState::Playing) {
            return mediaStartUs_;
        }
        int64_t elapsedNs = NowNs() - wallStartNs_;
        TimeUs elapsedUs = static_cast<TimeUs>(elapsedNs / 1000.0 * rate_.load(std::memory_order_relaxed));
        return mediaStartUs_ + elapsedUs;
    }

private:
    static int64_t NowNs() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::atomic<PlaybackState> state_{PlaybackState::Stopped};
    std::atomic<double> rate_{1.0};
    // These two are only ever written from the playback-control path
    // (Play/Pause/SeekTo), which the engine serializes through the command
    // queue, so plain members (not atomics) are safe and cheaper to read.
    TimeUs mediaStartUs_ = 0;
    int64_t wallStartNs_ = 0;
};

}  // namespace nle
