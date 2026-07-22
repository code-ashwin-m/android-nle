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
#include "effects/EffectRuntime.h"
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
    PlaybackEngine(Project* project, EffectRuntime* effectRuntime) : project_(project), effectRuntime_(effectRuntime) {}
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

    // Marks the shared RenderGraph stale so RebuildGraphIfNeeded rebuilds it
    // from scratch on the render thread's next loop iteration, instead of
    // reusing node instances that no longer match the timeline's current
    // clip/effect structure.
    //
    // This didn't exist before Phase 2: RebuildGraphIfNeeded's dirty flag
    // was previously cleared only once, in AttachPreviewSurface, which
    // happened to be harmless while Brightness was the only effect (its
    // per-track node already re-reads the active clip's property fresh
    // every frame, so nothing about it goes stale). Packaged effects and
    // per-clip transforms are not fully robust to that gap -- a clip added
    // or an effect attached *after* the preview surface is already up would
    // silently never appear -- so every structural EditorEngine entry point
    // (AddClip, DeleteClip, SplitClip, TrimClipHead/Tail, AddTrack,
    // AddEffect, AddPackagedEffect) now calls this. See
    // docs/ARCHITECTURE.md, "Existing files modified" for the full list and
    // reasoning.
    void InvalidateGraph() { graphBuilt_.store(false, std::memory_order_release); }

private:
    void RenderLoop();
    void RebuildGraphIfNeeded();
    double FrameDurationSeconds() const { return 1.0 / project_->Settings().fps; }

    Project* project_;
    EffectRuntime* effectRuntime_;
    PlaybackClock clock_;
    double playbackRate_ = 1.0;
    std::atomic<PreviewQuality> previewQuality_{PreviewQuality::Full};

    std::unique_ptr<GLContext> previewGlContext_;
    std::unique_ptr<OpenGLRenderer> previewRenderer_;
    std::unique_ptr<TexturePool> texturePool_;
    RenderGraph graph_;
    std::atomic<bool> graphBuilt_{false};  // written from both the calling thread (InvalidateGraph) and the render thread (RebuildGraphIfNeeded)

    std::thread renderThread_;
    std::atomic<bool> running_{false};

    RenderStats stats_;
};

}  // namespace nle
