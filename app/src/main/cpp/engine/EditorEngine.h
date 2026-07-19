// EditorEngine.h
//
// "The C++ engine is the single source of truth... Kotlin only mirrors the
// state exposed by C++." This class is that single source of truth, and it
// is deliberately the *only* class the JNI bridge (jni/jni_bridge.cpp)
// talks to. Every other class in this codebase (Project, PlaybackEngine,
// CommandStack, Decoder...) is reachable only through EditorEngine, which
// is what lets the JNI layer stay "extremely thin" -- it has nothing to
// decide, because every decision point is a method call into here.
//
// Threading model: all mutating calls (AddClip, SetBrightness, Undo, Redo,
// Play, Pause, Seek...) are expected to arrive from the JNI thread (i.e.
// whatever Kotlin thread called the external fun) and are executed
// synchronously against CommandStack/Project on that calling thread. This
// is safe because CommandStack itself has no internal locking and assumes
// single-threaded mutation (see command/CommandStack.h) -- EditorEngine is
// the one place that promise is upheld, by never letting the render or
// decoder threads touch Project or CommandStack directly. The render
// thread only *reads* Project (via RenderContext::time driving
// Track::ClipAt / Property::ValueAt lookups), which is safe unguarded only
// because Phase 1 doesn't yet mutate Project concurrently with playback in
// a way that matters; the moment a future phase needs edits to be
// race-free against started of the render loop, either mutation is
// batched onto the render thread as with a real editor's tenuring/GC
// pause pattern, this is the seam.

#pragma once

#include <memory>
#include <string>

#include "command/CommandStack.h"
#include "core/Project.h"
#include "encode/Encoder.h"
#include "playback/PlaybackEngine.h"

namespace nle {

class EditorEngine {
public:
    EditorEngine();
    ~EditorEngine();

    // Project lifecycle. Serialization (reading/writing a .nleproj file)
    // is intentionally left as a follow-up ProjectStore class -- see
    // core/Project.h's header comment -- so this scaffold's project
    // methods work purely in-memory.
    void CreateProject(const std::string& name, ProjectSettings settings);
    bool OpenProject(const std::string& path);
    bool SaveProject(const std::string& path) const;
    void RenameProject(const std::string& newName);

    Project* CurrentProject() { return project_.get(); }

    // "Kotlin only mirrors the state exposed by C++" is implemented
    // concretely as: Kotlin asks for a full snapshot of the current
    // project as JSON, parses it with org.json (already part of the
    // Android SDK -- no extra Kotlin dependency needed), and replaces its
    // entire UiProject tree with the result. This is deliberately a full
    // snapshot, not an incremental diff -- see EditorState.kt's header
    // comment for why that's the right tradeoff at Phase 1's data volumes.
    // EditorViewModel polls this after every mutating call.
    std::string GetProjectSnapshotJson() const;

    // Media Panel.
    MediaSourceId ImportMedia(const std::string& uri, MediaType type);

    // Timeline mutation entry points. Each of these constructs a Command
    // and routes it through CommandStack::Execute -- see command/Command.h
    // for why every mutation takes this path instead of touching Project
    // fields directly.
    TrackId AddTrack(TrackType type);
    ClipId AddClip(TrackId track, MediaSourceId source, TimeUs timelineStart, TimeUs sourceIn, TimeUs sourceOut);
    void DeleteClip(TrackId track, ClipId clip, bool ripple);
    void SplitClip(TrackId track, ClipId clip, TimeUs atTime);
    void TrimClipHead(TrackId track, ClipId clip, TimeUs newSourceIn);
    void TrimClipTail(TrackId track, ClipId clip, TimeUs newSourceOut);

    void SetBrightness(TrackId track, ClipId clip, EffectId effect, double value);
    EffectId AddEffect(TrackId track, ClipId clip, EffectType type, double defaultValue);

    void Undo();
    void Redo();
    bool CanUndo() const { return commandStack_.CanUndo(); }
    bool CanRedo() const { return commandStack_.CanRedo(); }

    // Playback / preview.
    PlaybackEngine& Playback() { return *playback_; }

    // Export. Owns its own EncoderThread-equivalent loop internally;
    // exposed as start/poll rather than a single blocking call so JNI can
    // report export progress back to Compose without blocking the calling
    // thread for the whole export duration.
    bool StartExport(const EncoderConfig& config);
    float ExportProgress() const { return exportProgress_; }
    bool IsExporting() const { return exporting_; }
    void CancelExport();

private:
    std::unique_ptr<Project> project_;
    CommandStack commandStack_;
    std::unique_ptr<PlaybackEngine> playback_;

    std::atomic<bool> exporting_{false};
    std::atomic<float> exportProgress_{0.0f};
};

}  // namespace nle
