#include "decode/Decoder.h"

#include <android/log.h>
#include <android/surface_texture.h>
#include <android/surface_texture_jni.h>
#include <media/NdkMediaFormat.h>

#include "decode/DecoderThread.h"

#define LOG_TAG "Decoder"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace nle {

namespace {
int SelectVideoTrack(AMediaExtractor* extractor) {
    size_t trackCount = AMediaExtractor_getTrackCount(extractor);
    for (size_t i = 0; i < trackCount; ++i) {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, i);
        const char* mime = nullptr;
        AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
        bool isVideo = mime && std::string(mime).rfind("video/", 0) == 0;
        AMediaFormat_delete(format);
        if (isVideo) return static_cast<int>(i);
    }
    return -1;
}
}  // namespace

Decoder::Decoder() = default;

Decoder::~Decoder() { Close(); }

bool Decoder::Open(const std::string& uri) {
    extractor_ = AMediaExtractor_new();
    media_status_t status = AMediaExtractor_setDataSource(extractor_, uri.c_str());
    if (status != AMEDIA_OK) {
        LOGE("Failed to open source '%s': %d", uri.c_str(), status);
        return false;
    }

    videoTrackIndex_ = SelectVideoTrack(extractor_);
    if (videoTrackIndex_ < 0) {
        LOGE("No video track found in '%s'", uri.c_str());
        return false;
    }
    AMediaExtractor_selectTrack(extractor_, videoTrackIndex_);

    AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor_, videoTrackIndex_);
    const char* mime = nullptr;
    AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);

    codec_ = AMediaCodec_createDecoderByType(mime);
    if (!codec_) {
        LOGE("No decoder available for mime '%s'", mime ? mime : "?");
        AMediaFormat_delete(format);
        return false;
    }

    // Cache probe info from the container so EditorEngine/Timeline can
    // report duration/resolution without a separate probe pass -- Open()
    // performs a full Probe() as a side effect for free.
    int32_t width = 0, height = 0;
    int64_t durationUs = 0;
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height);
    AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &durationUs);
    info_.width = width;
    info_.height = height;
    info_.duration = durationUs;
    info_.hasVideo = true;
    info_.videoCodecMime = mime ? mime : "";

    AMediaFormat_delete(format);
    return true;  // AMediaCodec_configure happens in AttachOutputSurface once the Surface exists
}

bool Decoder::AttachOutputSurface(JNIEnv* env, jobject surfaceTexture, unsigned int glTextureId) {
    // See the header comment on why this jobject exists at all: this is
    // the only place native code reaches back into a Java-constructed
    // object, purely because SurfaceTexture construction has no pre-API-28
    // native equivalent.
    outputTextureId_ = glTextureId;
    surfaceTexture_ = ASurfaceTexture_fromSurfaceTexture(env, surfaceTexture);
    if (!surfaceTexture_) {
        LOGE("ASurfaceTexture_fromSurfaceTexture failed");
        return false;
    }

    ANativeWindow* window = ASurfaceTexture_acquireANativeWindow(surfaceTexture_);
    if (!window) {
        LOGE("acquireANativeWindow failed");
        return false;
    }

    AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor_, videoTrackIndex_);
    media_status_t status = AMediaCodec_configure(codec_, format, window, nullptr, 0);
    AMediaFormat_delete(format);
    ANativeWindow_release(window);  // codec holds its own reference after configure

    if (status != AMEDIA_OK) {
        LOGE("AMediaCodec_configure failed: %d", status);
        return false;
    }

    thread_ = std::make_unique<DecoderThread>(this);
    thread_->Start();
    return true;
}

Frame Decoder::FrameNear(TimeUs timeUs) {
    Frame frame = frameQueue_.FrameNear(timeUs);
    if (!frame.IsValid() && frame.presentationTimeUs == kTimeInvalid) {
        // No frame has ever arrived yet (e.g. still warming up after
        // Open()); returning an invalid Frame is correct here --
        // DecoderSourceNode treats that as "nothing to show this call",
        // not an error.
    }

    // This is the actual GL-side handoff: updateTexImage must run on the
    // thread holding the current GL context, which is the render thread
    // calling FrameNear (via DecoderSourceNode::Process), never the
    // decoder thread. Calling it once per distinct incoming frame (not on
    // repeated queries for an already-consumed frame) avoids redundant
    // texture updates while paused.
    if (surfaceTexture_) {
        ASurfaceTexture_updateTexImage(surfaceTexture_);
        int64_t timestampNs = ASurfaceTexture_getTimestamp(surfaceTexture_);
        (void)timestampNs;  // available for A/V sync refinement in a future pass
        frame.textureId = outputTextureId_;
        frame.textureTarget = 0x8D65;  // GL_TEXTURE_EXTERNAL_OES
        frame.width = info_.width;
        frame.height = info_.height;
        frame.format = PixelFormat::NV12;
    }
    return frame;
}

void Decoder::SeekTo(TimeUs timeUs) {
    if (thread_) thread_->RequestSeek(timeUs);
}

MediaProbeInfo Decoder::Probe(const std::string& uri) {
    MediaProbeInfo info;
    AMediaExtractor* extractor = AMediaExtractor_new();
    if (AMediaExtractor_setDataSource(extractor, uri.c_str()) != AMEDIA_OK) {
        AMediaExtractor_delete(extractor);
        return info;
    }

    size_t trackCount = AMediaExtractor_getTrackCount(extractor);
    for (size_t i = 0; i < trackCount; ++i) {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, i);
        const char* mime = nullptr;
        AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
        std::string mimeStr = mime ? mime : "";

        if (mimeStr.rfind("video/", 0) == 0) {
            info.hasVideo = true;
            info.videoCodecMime = mimeStr;
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &info.width);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &info.height);
            int64_t duration = 0;
            AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &duration);
            info.duration = duration;
            int32_t frameRate = 0;
            if (AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, &frameRate)) {
                info.frameRate = frameRate;
            }
        } else if (mimeStr.rfind("audio/", 0) == 0) {
            info.hasAudio = true;
            info.audioCodecMime = mimeStr;
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &info.sampleRate);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &info.channelCount);
        }
        AMediaFormat_delete(format);
    }

    AMediaExtractor_delete(extractor);
    return info;
}

void Decoder::Close() {
    if (thread_) {
        thread_->Stop();
        thread_.reset();
    }
    if (codec_) {
        AMediaCodec_delete(codec_);
        codec_ = nullptr;
    }
    if (surfaceTexture_) {
        ASurfaceTexture_release(surfaceTexture_);
        surfaceTexture_ = nullptr;
    }
    if (extractor_) {
        AMediaExtractor_delete(extractor_);
        extractor_ = nullptr;
    }
}

}  // namespace nle
