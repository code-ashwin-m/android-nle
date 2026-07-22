#include "playback/PlaybackEngine.h"

#include <android/log.h>

#include "rendergraph/nodes/BrightnessNode.h"
#include "rendergraph/nodes/ClipTransformNode.h"
#include "rendergraph/nodes/ColorConvertNode.h"
#include "rendergraph/nodes/CompositeNode.h"
#include "rendergraph/nodes/DecoderSourceNode.h"
#include "rendergraph/nodes/OutputNode.h"
#include "rendergraph/nodes/PackagedEffectSlotNode.h"

#define LOG_TAG "PlaybackEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace nle {

PlaybackEngine::~PlaybackEngine() { DetachPreviewSurface(); }

bool PlaybackEngine::AttachPreviewSurface(void* nativeWindow) {
    previewGlContext_ = std::make_unique<GLContext>();
    if (!previewGlContext_->Initialize(nativeWindow)) return false;
    previewGlContext_->MakeCurrent();

    previewRenderer_ = std::make_unique<OpenGLRenderer>();
    texturePool_ = std::make_unique<TexturePool>();

    graphBuilt_ = false;  // force RebuildGraphIfNeeded to run on first render loop iteration

    running_.store(true, std::memory_order_release);
    renderThread_ = std::thread(&PlaybackEngine::RenderLoop, this);
    return true;
}

void PlaybackEngine::DetachPreviewSurface() {
    if (running_.exchange(false)) {
        if (renderThread_.joinable()) renderThread_.join();
    }
    graph_.DetachAll();
    previewRenderer_.reset();
    previewGlContext_.reset();
    texturePool_.reset();
}

void PlaybackEngine::SeekTo(TimeUs timeUs) {
    clock_.SeekTo(timeUs);
    // Decoder-level seeking (nearest keyframe + decode-forward) is
    // triggered per-track the next time DecoderSourceNode::Process runs
    // and notices the requested time jumped discontinuously -- see the
    // note in DecoderSourceNode; Phase 1 keeps that check inline there
    // rather than duplicating seek-detection logic here.
    clock_.MarkSeekComplete();
}

void PlaybackEngine::StepFrame(int deltaFrames) {
    TimeUs frameUs = static_cast<TimeUs>(FrameDurationSeconds() * 1'000'000.0);
    SeekTo(clock_.CurrentTimeUs() + frameUs * deltaFrames);
}

void PlaybackEngine::RebuildGraphIfNeeded() {
    if (graphBuilt_) return;

    RenderContext attachContext;
    attachContext.graphicsDevice = previewRenderer_.get();
    attachContext.texturePool = texturePool_.get();
    attachContext.glContext = previewGlContext_.get();

    graph_ = RenderGraph{};  // Phase 1: full rebuild on structural change, not incremental patching

    // Composited incrementally, bottom-to-top (Timeline::Tracks() is
    // already in that order -- see core/Timeline.h), rather than collecting
    // every video track's output and blending all of them in one final
    // N-input CompositeNode the way Phase 1 did. That collect-then-blend
    // shape can't support Adjustment tracks, because an adjustment layer's
    // effect stack needs to see the specific composite of *only the layers
    // beneath it* -- not the fully-finished frame -- as its input. Tracking
    // a running `compositeSoFar` handle and folding each track into it in
    // order gives every Adjustment track exactly that, for free, using
    // CompositeNode unchanged (each fold is just a 2-input composite, which
    // is the N=2 case of what CompositeNode already handled).
    NodeHandle compositeSoFar = kInvalidNode;
    for (auto& track : project_->GetTimeline().Tracks()) {
        if (track->Type() == TrackType::Video) {
            NodeHandle source = graph_.AddNode(std::make_unique<DecoderSourceNode>(project_, track->Id()));
            NodeHandle colorConvert = graph_.AddNode(std::make_unique<ColorConvertNode>());
            NodeHandle brightness = graph_.AddNode(std::make_unique<BrightnessNode>(project_, track->Id()));
            // Packaged effects (Dreamy, Cyberpunk, ...) run before the
            // clip's own transform, matching every professional NLE's
            // convention that "effects" operate in the clip's native frame
            // and "transform" places the (already-effected) result within
            // the canvas.
            NodeHandle packagedEffects =
                graph_.AddNode(std::make_unique<PackagedEffectSlotNode>(project_, track->Id(), effectRuntime_));
            NodeHandle transform = graph_.AddNode(std::make_unique<ClipTransformNode>(project_, track->Id()));

            graph_.Connect(source, colorConvert);
            graph_.Connect(colorConvert, brightness);
            graph_.Connect(brightness, packagedEffects);
            graph_.Connect(packagedEffects, transform);

            if (compositeSoFar == kInvalidNode) {
                compositeSoFar = transform;
            } else {
                NodeHandle composite = graph_.AddNode(std::make_unique<CompositeNode>());
                graph_.Connect(compositeSoFar, composite);
                graph_.Connect(transform, composite);
                compositeSoFar = composite;
            }
        } else if (track->Type() == TrackType::Adjustment) {
            // Adjustment Layer Animation (spec): an adjustment track with
            // nothing beneath it yet has nothing to adjust -- skip rather
            // than adjusting a black/empty frame.
            if (compositeSoFar == kInvalidNode) continue;

            NodeHandle brightness = graph_.AddNode(std::make_unique<BrightnessNode>(project_, track->Id()));
            NodeHandle packagedEffects =
                graph_.AddNode(std::make_unique<PackagedEffectSlotNode>(project_, track->Id(), effectRuntime_));
            graph_.Connect(compositeSoFar, brightness);
            graph_.Connect(brightness, packagedEffects);
            compositeSoFar = packagedEffects;
        }
        // Audio/Sticker/Text tracks: not part of the video render graph yet
        // -- see docs/ARCHITECTURE.md's "Not yet implemented" list.
    }

    NodeHandle output = graph_.AddNode(std::make_unique<PreviewOutputNode>(previewGlContext_.get()));
    if (compositeSoFar != kInvalidNode) {
        graph_.Connect(compositeSoFar, output);
    }
    graph_.SetOutputNode(output);

    graph_.AttachAll(attachContext);
    graphBuilt_ = true;
}

void PlaybackEngine::RenderLoop() {
    previewGlContext_->MakeCurrent();

    using Clock = std::chrono::steady_clock;
    auto lastStatsWindow = Clock::now();
    int framesThisWindow = 0;

    while (running_.load(std::memory_order_acquire)) {
        auto frameStart = Clock::now();

        RebuildGraphIfNeeded();

        PreviewQuality quality = previewQuality_.load(std::memory_order_relaxed);
        int divisor = quality == PreviewQuality::Full ? 1 : quality == PreviewQuality::Half ? 2 : 4;

        RenderContext context;
        context.time = clock_.CurrentTimeUs();
        context.texturePool = texturePool_.get();
        context.glContext = previewGlContext_.get();
        context.graphicsDevice = previewRenderer_.get();
        context.outputWidth = project_->Settings().width / divisor;
        context.outputHeight = project_->Settings().height / divisor;

        graph_.Execute(context);

        framesThisWindow++;
        auto now = Clock::now();
        double windowSeconds = std::chrono::duration<double>(now - lastStatsWindow).count();
        if (windowSeconds >= 1.0) {
            stats_.currentFps = framesThisWindow / windowSeconds;
            stats_.rendererFps = stats_.currentFps;  // Phase 1: render thread paces at output rate;
                                                       // decoderFps gets its own counter once decode
                                                       // and render rates can diverge under load.
            framesThisWindow = 0;
            lastStatsWindow = now;
        }

        // Frame pacing: sleep for whatever's left of this frame's budget.
        // If a frame overruns its budget, we do not sleep negative time and
        // do not try to "catch up" by skipping the sleep on the next frame
        // either -- PlaybackClock derives time from the wall clock (see
        // core/PlaybackClock.h), so a late frame here shows up as a
        // slightly later requested `context.time` next iteration, not as
        // accumulated drift.
        double elapsed = std::chrono::duration<double>(Clock::now() - frameStart).count();
        double budget = FrameDurationSeconds();
        if (elapsed < budget) {
            std::this_thread::sleep_for(std::chrono::duration<double>(budget - elapsed));
        } else {
            stats_.droppedFrames++;
        }
    }
}

}  // namespace nle
