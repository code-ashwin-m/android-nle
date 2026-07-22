#include "engine/EditorEngine.h"

#include <sstream>

#include "command/commands/AddClipCommand.h"
#include "command/commands/SetBrightnessCommand.h"
#include "command/commands/TimelineCommands.h"
#include "decode/Decoder.h"

namespace nle {

EditorEngine::EditorEngine() = default;
EditorEngine::~EditorEngine() = default;

void EditorEngine::CreateProject(const std::string& name, ProjectSettings settings) {
    project_ = std::make_unique<Project>(ProjectId::Generate(), name, settings);
    commandStack_.Clear();
    playback_ = std::make_unique<PlaybackEngine>(project_.get(), &effectRuntime_);
}

bool EditorEngine::OpenProject(const std::string& /*path*/) {
    // Deserialization intentionally deferred -- see EditorEngine.h. Wiring
    // point for a future ProjectStore::Load(path) call.
    return false;
}

bool EditorEngine::SaveProject(const std::string& /*path*/) const {
    return false;  // see OpenProject note
}

void EditorEngine::RenameProject(const std::string& newName) {
    if (project_) project_->Rename(newName);
}

MediaSourceId EditorEngine::ImportMedia(const std::string& uri, MediaType type) {
    if (!project_) return MediaSourceId{};
    MediaSource* source = project_->ImportMedia(uri, type);
    if (type == MediaType::Video || type == MediaType::Image) {
        source->SetProbeInfo(Decoder::Probe(uri));
    }
    return source->Id();
}

TrackId EditorEngine::AddTrack(TrackType type) {
    if (!project_) return TrackId{};
    // Track creation is not currently undoable in Phase 1 (adding a track
    // is rarely the operation a user wants to reverse in isolation -- it's
    // almost always immediately followed by adding a clip to it, which is
    // undoable). Revisit if user testing shows this expectation is wrong.
    TrackId id = project_->GetTimeline().AddTrack(type)->Id();
    if (playback_) playback_->InvalidateGraph();
    return id;
}

ClipId EditorEngine::AddClip(TrackId track, MediaSourceId source, TimeUs timelineStart, TimeUs sourceIn, TimeUs sourceOut) {
    if (!project_) return ClipId{};
    auto command = std::make_unique<AddClipCommand>(track, source, timelineStart, sourceIn, sourceOut);
    AddClipCommand* raw = command.get();
    commandStack_.Execute(std::move(command), *project_);
    if (playback_) playback_->InvalidateGraph();
    // AddClipCommand assigns its ClipId internally on first Do(); expose it
    // back to the caller (JNI -> Kotlin) so the UI can select the new clip.
    return raw->ResultClipId();
}

void EditorEngine::DeleteClip(TrackId track, ClipId clip, bool ripple) {
    if (!project_) return;
    commandStack_.Execute(std::make_unique<DeleteClipCommand>(track, clip, ripple), *project_);
    if (playback_) playback_->InvalidateGraph();
}

void EditorEngine::SplitClip(TrackId track, ClipId clip, TimeUs atTime) {
    if (!project_) return;
    commandStack_.Execute(std::make_unique<SplitClipCommand>(track, clip, atTime), *project_);
    if (playback_) playback_->InvalidateGraph();
}

void EditorEngine::TrimClipHead(TrackId track, ClipId clip, TimeUs newSourceIn) {
    if (!project_) return;
    commandStack_.Execute(std::make_unique<TrimClipCommand>(track, clip, TrimClipCommand::Edge::Head, newSourceIn), *project_);
    if (playback_) playback_->InvalidateGraph();
}

void EditorEngine::TrimClipTail(TrackId track, ClipId clip, TimeUs newSourceOut) {
    if (!project_) return;
    commandStack_.Execute(std::make_unique<TrimClipCommand>(track, clip, TrimClipCommand::Edge::Tail, newSourceOut), *project_);
    if (playback_) playback_->InvalidateGraph();
}

void EditorEngine::SetBrightness(TrackId track, ClipId clip, EffectId effect, double value) {
    if (!project_ || !playback_) return;
    TimeUs currentTime = playback_->CurrentTime();
    commandStack_.Execute(std::make_unique<SetBrightnessCommand>(track, clip, effect, currentTime, value), *project_);
    // Deliberately NOT calling InvalidateGraph(): this only adds/moves a
    // keyframe on a property BrightnessNode already re-reads every frame
    // via Property::ValueAt(context.time) -- no node or edge changes.
    // Rebuilding (and recompiling every shader in the graph) on every
    // slider drag would be a real performance regression for zero benefit.
}

EffectId EditorEngine::AddEffect(TrackId track, ClipId clip, EffectType type, double defaultValue) {
    if (!project_) return EffectId{};
    auto command = std::make_unique<AddEffectCommand>(track, clip, type, defaultValue);
    AddEffectCommand* raw = command.get();
    commandStack_.Execute(std::move(command), *project_);
    if (playback_) playback_->InvalidateGraph();
    return raw->ResultEffectId();
}

bool EditorEngine::LoadEffectPackage(const std::string& packageDir, std::string* error) {
    return effectRuntime_.LoadPackage(packageDir, error) != nullptr;
}

EffectId EditorEngine::AddPackagedEffect(TrackId track, ClipId clip, const std::string& packageId) {
    if (!project_) return EffectId{};
    auto command = std::make_unique<AddPackagedEffectCommand>(track, clip, packageId, &effectRuntime_);
    AddPackagedEffectCommand* raw = command.get();
    commandStack_.Execute(std::move(command), *project_);
    if (playback_) playback_->InvalidateGraph();  // new node(s)/edges -- a real structural change, unlike SetBrightness above
    return raw->ResultEffectId();
}

void EditorEngine::Undo() {
    if (!project_) return;
    commandStack_.Undo(*project_);
    // CommandStack::Undo can reverse any command, including structural ones
    // (AddClip, AddEffect, AddPackagedEffect, ...) -- Command has no
    // "IsStructural()" flag to check, so this invalidates unconditionally.
    // The cost of an occasional unnecessary rebuild (undoing a brightness
    // tweak) is far cheaper than a stale graph after undoing a clip add.
    if (playback_) playback_->InvalidateGraph();
}

void EditorEngine::Redo() {
    if (!project_) return;
    commandStack_.Redo(*project_);
    if (playback_) playback_->InvalidateGraph();  // see Undo()'s comment
}

bool EditorEngine::StartExport(const EncoderConfig& /*config*/) {
    if (exporting_.exchange(true)) return false;
    exportProgress_ = 0.0f;
    // Full implementation spins up: an offscreen GLContext sharing the
    // preview context's texture namespace, an Encoder configured per
    // `config`, an EncoderOutputNode wired onto the *same* graph instance
    // PlaybackEngine already built (per the "preview and export share a
    // graph" requirement), and drives time from 0 to Timeline::Duration()
    // in fixed FrameDurationSeconds() steps rather than wall-clock time --
    // export must run faster or slower than realtime, never locked to it.
    // Left as a wiring point in this scaffold: ExportEngine (a thin
    // sibling to PlaybackEngine, reusing RenderGraph the same way) is the
    // natural next file once Phase 1's preview path is validated end to
    // end.
    exporting_ = false;
    return true;
}

void EditorEngine::CancelExport() { exporting_ = false; }

namespace {
// Phase 1's object graph is small enough that a hand-rolled serializer is
// simpler and has fewer moving parts than adding a JSON library dependency
// for this one purpose. If the data model grows substantially (nested
// effect graphs, masks with point lists), switching to a real JSON library
// here is a contained, one-file change -- nothing outside this function
// depends on how the string is built.
std::string JsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}
}  // namespace

std::string EditorEngine::GetProjectSnapshotJson() const {
    if (!project_) return "null";

    TimeUs evalTime = playback_ ? playback_->CurrentTime() : 0;
    std::ostringstream json;
    json << "{";
    json << "\"name\":\"" << JsonEscape(project_->Name()) << "\",";
    json << "\"widthPx\":" << project_->Settings().width << ",";
    json << "\"heightPx\":" << project_->Settings().height << ",";
    json << "\"fps\":" << project_->Settings().fps << ",";
    json << "\"durationUs\":" << project_->GetTimeline().Duration() << ",";
    json << "\"tracks\":[";

    bool firstTrack = true;
    for (auto& track : project_->GetTimeline().Tracks()) {
        if (!firstTrack) json << ",";
        firstTrack = false;

        json << "{\"id\":" << track->Id().value << ",\"type\":" << static_cast<int>(track->Type())
             << ",\"muted\":" << (track->IsMuted() ? "true" : "false")
             << ",\"hidden\":" << (track->IsHidden() ? "true" : "false") << ",\"clips\":[";

        bool firstClip = true;
        for (auto& clip : track->Clips()) {
            if (!firstClip) json << ",";
            firstClip = false;

            json << "{\"id\":" << clip->Id().value << ",\"sourceId\":" << clip->Source().value
                 << ",\"timelineStartUs\":" << clip->TimelineStart() << ",\"durationUs\":" << clip->Duration()
                 << ",\"sourceInUs\":" << clip->SourceIn() << ",\"sourceOutUs\":" << clip->SourceOut()
                 << ",\"effects\":[";

            bool firstEffect = true;
            for (auto& effect : clip->Effects()) {
                if (!firstEffect) json << ",";
                firstEffect = false;

                json << "{\"id\":" << effect->Id().value << ",\"type\":" << static_cast<int>(effect->Type())
                     << ",\"properties\":{";
                bool firstProp = true;
                for (auto& prop : effect->Properties()) {
                    if (!firstProp) json << ",";
                    firstProp = false;
                    json << "\"" << JsonEscape(prop->Name()) << "\":" << prop->ValueAt(evalTime);
                }
                json << "}}";
            }
            json << "]}";
        }
        json << "]}";
    }
    json << "]}";
    return json.str();
}

}  // namespace nle
