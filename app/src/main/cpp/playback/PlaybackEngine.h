// PlaybackEngine.h
//
// Owns the dedicated render thread the spec requires ("Never render on UI
// thread") and the one RenderGraph instance shared between preview and
// export (only the terminal OutputNode differs -- see
// rendergraph/nodes/OutputNode.h). This class is the thing EditorEngine's
// Play/Pause/Seek JNI-forwarded calls actually reach.
//
// Render statistics (FPS, decoder FPS, renderer FPS, dropped frames, GPU
// time) are accumulated here rather than in RenderGraph, because they're
// about *scheduling* (did we hit our frame deadline, did we have to skip a
// frame) not about any individual node's correctness -- keeping them out
// of RenderGraph/RenderNode keeps those classes focused purely on
// producing correct pixels.

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "core/PlaybackClock.h"
#include "core/Project.h"
#include "render/GLContext.h"
#include "render/OpenGLRenderer.h"
#include "render/TexturePool.h"
#include "rendergraph/RenderGraph.h"

namespace nle {

struct RenderStats {
    double currentFps = 0.0;
    double decoderFps = 0.0;
    double rendererFps = 0.0;
    int droppedFrames = 0;
    double lastGpuTimeMs = 0.0;
};

enum class PreviewQuality { Full, Half, Quarter };

class PlaybackEngine {
public:
    explicit PlaybackEngine(Project* project) : project_(project) {}
    ~PlaybackEngine();

    // Called once a preview Surface exists (i.e. the Compose UI's
    // AndroidView/SurfaceView has been created). Builds the shared graph
    // wired to a PreviewOutputNode and starts the render thread.
    bool AttachPreviewSurface(void* nativeWindow);
    void DetachPreviewSurface();

    void Play() { clock_.Play(playbackRate_); }
    void Pause() { clock_.Pause(); }
    void Stop() { clock_.Stop(); }
    void SeekTo(TimeUs timeUs);
    void StepFrame(int deltaFrames);  // frame stepping, spec-required transport control
    void SetPlaybackRate(double rate) { playbackRate_ = rate; }
    void SetPreviewQuality(PreviewQuality quality) { previewQuality_.store(quality, std::memory_order_relaxed); }

    TimeUs CurrentTime() const { return clock_.CurrentTimeUs(); }
    PlaybackState State() const { return clock_.State(); }
    RenderStats Stats() const { return stats_; }

private:
    void RenderLoop();
    void RebuildGraphIfNeeded();
    double FrameDurationSeconds() const { return 1.0 / project_->Settings().fps; }

    Project* project_;
    PlaybackClock clock_;
    double playbackRate_ = 1.0;
    std::atomic<PreviewQuality> previewQuality_{PreviewQuality::Full};

    std::unique_ptr<GLContext> previewGlContext_;
    std::unique_ptr<OpenGLRenderer> previewRenderer_;
    std::unique_ptr<TexturePool> texturePool_;
    RenderGraph graph_;
    bool graphBuilt_ = false;

    std::thread renderThread_;
    std::atomic<bool> running_{false};

    RenderStats stats_;
};

}  // namespace nle
