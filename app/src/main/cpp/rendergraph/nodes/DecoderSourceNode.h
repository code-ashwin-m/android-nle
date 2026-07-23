// DecoderSourceNode.h
//
// The first node in the chain from the spec's diagram: "Video Frame ->
// Decoder Node". One instance exists per *track*, not per clip -- as the
// playhead crosses clip boundaries within a track, this node is
// responsible for swapping which underlying Decoder it's pulling from,
// so upstream graph structure doesn't need to change every time a clip
// boundary is crossed.
//
// This node does not itself decode; it owns a Decoder (decode/Decoder.h)
// per MediaSource it has opened and asks it for the frame nearest to the
// requested time. Actual MediaCodec/MediaExtractor work happens on the
// decoder thread (decode/DecoderThread.h); this node's Process() call,
// which runs on the render thread, only reads whatever frame the decoder
// thread has already queued -- it must never block waiting on I/O.
//
// Opening a new Decoder (the first time a given MediaSource is
// encountered) does do real work on this call, though: it allocates a GL
// texture name and constructs the android.graphics.SurfaceTexture that
// texture will be bound to. Both of those must happen on the render
// thread specifically -- glGenTextures needs the current GL context,
// which only the render thread holds, and this is also why this
// allocation happens lazily on first Process() rather than eagerly when
// the clip is added to the timeline (that call happens on whichever
// thread received the JNI AddClip call, which has no GL context at all).

#pragma once

#include <GLES3/gl3.h>

#include <memory>
#include <unordered_map>

#include "core/Timeline.h"
#include "core/Track.h"
#include "decode/Decoder.h"
#include "jni/JniUtils.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class Project;

class DecoderSourceNode : public RenderNode {
public:
    DecoderSourceNode(Project* project, TrackId trackId) : project_(project), trackId_(trackId) {}
    ~DecoderSourceNode() override { ReleaseAllDecoders(); }

    std::string Name() const override { return "DecoderSource[" + std::to_string(trackId_.value) + "]"; }

    Frame Process(const std::vector<Frame>& /*inputs*/, const RenderContext& context) override {
        Track* track = TrackPtr();
        if (!track) return Frame{};

        Clip* clip = track->ClipAt(context.time);
        if (!clip) return Frame{};  // gap in the track at this time -- valid, not an error

        Decoder* decoder = DecoderForSource(clip->Source());
        if (!decoder) return Frame{};

        TimeUs sourceTime = clip->ToSourceTime(context.time);
        return decoder->FrameNear(sourceTime);
    }

private:
    struct OpenDecoder {
        std::unique_ptr<Decoder> decoder;
        jobject surfaceTextureGlobalRef = nullptr;  // kept alive for the decoder's lifetime; see comment below
    };

    Track* TrackPtr() const;  // implemented in .cpp, looks up via project_->GetTimeline()

    Decoder* DecoderForSource(MediaSourceId sourceId);

    // Decoder* DecoderForSource(MediaSourceId sourceId) {
    //     auto it = decoders_.find(sourceId.value);
    //     if (it != decoders_.end()) return it->second.decoder.get();

    //     MediaSource* source = project_->FindMedia(sourceId);
    //     if (!source) return nullptr;

    //     auto decoder = std::make_unique<Decoder>();
    //     if (!decoder->Open(source->Uri())) return nullptr;

    //     if (!AttachSurfaceTexture(*decoder, sourceId)) return nullptr;

    //     Decoder* ptr = decoder.get();
    //     decoders_[sourceId.value].decoder = std::move(decoder);
    //     return ptr;
    // }

    bool AttachSurfaceTexture(Decoder& decoder, MediaSourceId sourceId) {
        JNIEnv* env = GetJniEnvForCurrentThread();
        if (!env) return false;  // JavaVM not yet registered -- see jni/jni_bridge.cpp's JNI_OnLoad

        unsigned int textureId = 0;
        glGenTextures(1, &textureId);
        // GL_TEXTURE_EXTERNAL_OES textures require at least MIN/MAG filter
        // and CLAMP wrap set once before first use, per the Android
        // SurfaceTexture documentation.
        glBindTexture(0x8D65 /* GL_TEXTURE_EXTERNAL_OES */, textureId);
        glTexParameteri(0x8D65, 0x2801 /* GL_TEXTURE_MAG_FILTER */, 0x2601 /* GL_LINEAR */);
        glTexParameteri(0x8D65, 0x2800 /* GL_TEXTURE_MIN_FILTER */, 0x2601 /* GL_LINEAR */);

        jclass surfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
        jmethodID ctor = env->GetMethodID(surfaceTextureClass, "<init>", "(I)V");
        jobject surfaceTextureLocal = env->NewObject(surfaceTextureClass, ctor, static_cast<jint>(textureId));
        jobject surfaceTextureGlobal = env->NewGlobalRef(surfaceTextureLocal);
        env->DeleteLocalRef(surfaceTextureLocal);
        env->DeleteLocalRef(surfaceTextureClass);

        if (!decoder.AttachOutputSurface(env, surfaceTextureGlobal, textureId)) {
            env->DeleteGlobalRef(surfaceTextureGlobal);
            return false;
        }

        // The global ref must outlive the Decoder (ASurfaceTexture_fromSurfaceTexture
        // does not itself hold a JVM-visible reference), so it's stashed
        // in this node's map, released in ReleaseAllDecoders().
        decoders_[sourceId.value].surfaceTextureGlobalRef = surfaceTextureGlobal;
        return true;
    }

    void ReleaseAllDecoders() {
        JNIEnv* env = GetJniEnvForCurrentThread();
        for (auto& [id, entry] : decoders_) {
            if (env && entry.surfaceTextureGlobalRef) env->DeleteGlobalRef(entry.surfaceTextureGlobalRef);
        }
        decoders_.clear();
    }

    Project* project_;
    TrackId trackId_;
    std::unordered_map<uint64_t, OpenDecoder> decoders_;
};

}  // namespace nle
