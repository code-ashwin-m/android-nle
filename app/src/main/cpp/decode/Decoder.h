// Decoder.h
//
// Wraps one AMediaExtractor + AMediaCodec pair for one MediaSource, and
// owns the DecoderThread that drives them. This is the class that
// implements the spec's "MediaCodec Decoder -> Native Frames -> OpenGL
// Textures" pipeline -- specifically *without* ExoPlayer or VideoView, as
// required, using the NDK media APIs directly (media/NdkMediaExtractor.h,
// media/NdkMediaCodec.h).
//
// Surface plumbing (why this needs a jobject at all in an otherwise pure
// C++ engine): AMediaCodec decodes video most efficiently directly onto a
// Surface (hardware scaler + colorspace conversion, zero-copy into a
// GPU-importable buffer) rather than into a ByteBuffer. Getting a
// GL-sampleable texture out of that Surface requires a SurfaceTexture,
// and prior to the ASurfaceTexture NDK API (API 28), SurfaceTexture can
// only be *constructed* from Java/Kotlin -- there is no way to create one
// from pure native code on older APIs. So: EditorEngine's JNI entry point
// for opening a clip receives a `jobject surfaceTexture` that Kotlin
// constructed (new SurfaceTexture(textureId)) and passes it here, where
// ASurfaceTexture_fromSurfaceTexture wraps it for native use from then on.
// This is the one deliberate, minimal, documented exception to "no editor
// logic inside Kotlin" -- Kotlin here does zero decision-making, it only
// satisfies a platform API constraint by constructing an object type only
// the Java-side SDK can construct.
//
// Everything downstream of this handoff -- selecting the track, feeding
// the extractor, driving the codec, reading back the texture -- is native.

#pragma once

#include <jni.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include <atomic>
#include <memory>
#include <string>

#include "core/MediaSource.h"
#include "decode/FrameQueue.h"
#include "nle/core/Types.h"

struct ASurfaceTexture;

namespace nle {

class DecoderThread;

class Decoder {
public:
    Decoder();
    ~Decoder();

    // `env`/`surfaceTexture` come from the JNI call chain described above.
    // Returns false (and logs) on any setup failure -- missing file,
    // unsupported codec, etc. -- so EditorEngine can surface an import
    // error instead of crashing on first use.
    bool Open(const std::string& uri);
    bool AttachOutputSurface(JNIEnv* env, jobject surfaceTexture, unsigned int glTextureId);
    void Close();

    // Requests the frame nearest to (without exceeding) `timeUs`. Cheap:
    // reads from FrameQueue, does not itself touch the extractor/codec --
    // see DecoderThread for the actual decode loop.
    Frame FrameNear(TimeUs timeUs);

    // Professional seeking, per spec: seeks the extractor to the nearest
    // preceding keyframe, then DecoderThread decodes forward, discarding
    // frames, until it reaches the exact requested time. Returns
    // immediately; the seek completes asynchronously, and any in-flight
    // previous seek is superseded (see DecoderThread::RequestSeek).
    void SeekTo(TimeUs timeUs);

    // One-shot probe used by the Media Panel at import time, before any
    // Decoder instance needs to be kept alive for playback. Static and
    // self-contained: opens its own throwaway extractor, reads format
    // metadata, and closes it.
    static MediaProbeInfo Probe(const std::string& uri);

    const MediaProbeInfo& Info() const { return info_; }

private:
    friend class DecoderThread;

    AMediaExtractor* extractor_ = nullptr;
    AMediaCodec* codec_ = nullptr;
    ASurfaceTexture* surfaceTexture_ = nullptr;
    unsigned int outputTextureId_ = 0;

    int videoTrackIndex_ = -1;
    MediaProbeInfo info_;

    FrameQueue frameQueue_;
    std::unique_ptr<DecoderThread> thread_;
};

}  // namespace nle
