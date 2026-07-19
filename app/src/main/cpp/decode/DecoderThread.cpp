#include "decode/DecoderThread.h"

#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include <deque>

#include "decode/Decoder.h"
#include "decode/FrameQueue.h"

#define LOG_TAG "DecoderThread"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

namespace nle {

namespace {
// Callback-fed queues. AMediaCodec's async callbacks fire from a codec-
// internal thread (not this DecoderThread and not the caller), so these
// need real synchronization -- this is the one place a plain mutex is used
// instead of the lock-free SpscRingBuffer, because the producer side here
// is genuinely not under our control and isn't single-threaded by
// construction the way our own thread hops are.
std::mutex g_callbackMutex;
std::deque<int32_t> g_availableInputIndices;
struct OutputEvent {
    int32_t index;
    AMediaCodecBufferInfo info;
};
std::deque<OutputEvent> g_availableOutputs;
std::condition_variable g_callbackCv;

void OnInputAvailable(AMediaCodec*, void*, int32_t index) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_availableInputIndices.push_back(index);
    g_callbackCv.notify_one();
}

void OnOutputAvailable(AMediaCodec*, void*, int32_t index, AMediaCodecBufferInfo* info) {
    std::lock_guard<std::mutex> lock(g_callbackMutex);
    g_availableOutputs.push_back({index, *info});
    g_callbackCv.notify_one();
}

void OnFormatChanged(AMediaCodec*, void*, AMediaFormat*) {
    // Resolution/colorspace change mid-stream; Phase 1 has no adaptive
    // sources so this is a no-op, logged for visibility.
    LOGD("Output format changed");
}

void OnCodecError(AMediaCodec*, void*, media_status_t err, int32_t, const char* detail) {
    LOGE("Codec error %d: %s", err, detail ? detail : "");
}
}  // namespace

DecoderThread::DecoderThread(Decoder* owner) : owner_(owner) {}

DecoderThread::~DecoderThread() { Stop(); }

void DecoderThread::Start() {
    if (running_.exchange(true)) return;

    AMediaCodecOnAsyncNotifyCallback callbacks{OnInputAvailable, OnOutputAvailable, OnFormatChanged, OnCodecError};
    AMediaCodec_setAsyncNotifyCallback(owner_->codec_, callbacks, nullptr);
    AMediaCodec_start(owner_->codec_);

    thread_ = std::thread(&DecoderThread::Run, this);
}

void DecoderThread::Stop() {
    if (!running_.exchange(false)) return;
    seekCv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (owner_->codec_) AMediaCodec_stop(owner_->codec_);
}

void DecoderThread::RequestSeek(TimeUs timeUs) {
    {
        std::lock_guard<std::mutex> lock(seekMutex_);
        pendingSeekTime_ = timeUs;
        seekPending_.store(true, std::memory_order_release);
        seekGeneration_.fetch_add(1, std::memory_order_relaxed);
    }
    seekCv_.notify_one();
}

void DecoderThread::HandleSeekIfRequested() {
    if (!seekPending_.exchange(false, std::memory_order_acq_rel)) return;

    TimeUs target;
    {
        std::lock_guard<std::mutex> lock(seekMutex_);
        target = pendingSeekTime_;
    }

    // "Seek nearest keyframe. Decode forward." -- AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC
    // lands on the preceding keyframe; DrainOneOutputFrame then discards
    // every frame whose PTS is still short of `target` until it reaches
    // the exact requested frame.
    AMediaExtractor_seekTo(owner_->extractor_, target, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
    AMediaCodec_flush(owner_->codec_);
    owner_->frameQueue_.Flush();
    sawInputEOS_ = false;
    sawOutputEOS_ = false;

    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        g_availableInputIndices.clear();
        g_availableOutputs.clear();
    }
}

void DecoderThread::DecodeOneInputChunk() {
    int32_t index = -1;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        if (g_availableInputIndices.empty()) return;
        index = g_availableInputIndices.front();
        g_availableInputIndices.pop_front();
    }

    if (sawInputEOS_) return;

    uint8_t* buffer = nullptr;
    size_t bufferSize = 0;
    buffer = AMediaCodec_getInputBuffer(owner_->codec_, index, &bufferSize);
    if (!buffer) return;

    ssize_t sampleSize = AMediaExtractor_getSampleSize(owner_->extractor_);
    if (sampleSize < 0) {
        // End of stream: honor "Loop-safe behavior" by rewinding rather
        // than terminating playback outright, matching Phase 1's preview
        // default of looping.
        if (loop_) {
            AMediaExtractor_seekTo(owner_->extractor_, 0, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC);
            sampleSize = AMediaExtractor_getSampleSize(owner_->extractor_);
        }
        if (sampleSize < 0) {
            AMediaCodec_queueInputBuffer(owner_->codec_, index, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
            sawInputEOS_ = true;
            return;
        }
    }

    ssize_t read = AMediaExtractor_readSampleData(owner_->extractor_, buffer, bufferSize);
    int64_t pts = AMediaExtractor_getSampleTime(owner_->extractor_);
    AMediaCodec_queueInputBuffer(owner_->codec_, index, 0, static_cast<size_t>(read), pts, 0);
    AMediaExtractor_advance(owner_->extractor_);
}

void DecoderThread::DrainOneOutputFrame() {
    OutputEvent event;
    {
        std::lock_guard<std::mutex> lock(g_callbackMutex);
        if (g_availableOutputs.empty()) return;
        event = g_availableOutputs.front();
        g_availableOutputs.pop_front();
    }

    if (event.info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        sawOutputEOS_ = true;
        AMediaCodec_releaseOutputBuffer(owner_->codec_, event.index, false);
        return;
    }

    uint64_t myGeneration = seekGeneration_.load(std::memory_order_relaxed);

    // Rendering true hands this buffer to the output Surface (backed by
    // the ASurfaceTexture); the GL-visible texture image itself only
    // updates once the render thread calls ASurfaceTexture_updateTexImage,
    // which happens inside DecoderSourceNode::Process via Decoder::FrameNear
    // -- see Decoder.cpp for that half of the handoff.
    AMediaCodec_releaseOutputBuffer(owner_->codec_, event.index, true);

    // If a newer seek was requested while this frame was already in
    // flight through the codec, drop it silently instead of queueing --
    // this is the "cancel old seek requests" / "no stale frames" guarantee
    // from the spec, enforced at the exact point a stale frame would
    // otherwise reach FrameQueue.
    if (myGeneration != seekGeneration_.load(std::memory_order_relaxed)) return;

    Frame frame;
    frame.presentationTimeUs = event.info.presentationTimeUs;
    // textureId/target are filled in by Decoder::FrameNear at consume time
    // (post updateTexImage), not here -- see the rationale in Decoder.cpp.
    owner_->frameQueue_.PushDecoded(frame);
}

void DecoderThread::Run() {
    std::unique_lock<std::mutex> seekLock(seekMutex_, std::defer_lock);
    while (running_.load(std::memory_order_acquire)) {
        HandleSeekIfRequested();
        DecodeOneInputChunk();
        DrainOneOutputFrame();

        // Brief wait rather than a hot spin when neither queue has work --
        // the callbacks wake this immediately when new work arrives, so
        // this timeout only matters as a safety net.
        std::unique_lock<std::mutex> lock(g_callbackMutex);
        if (g_availableInputIndices.empty() && g_availableOutputs.empty()) {
            g_callbackCv.wait_for(lock, std::chrono::milliseconds(4));
        }
    }
}

}  // namespace nle
