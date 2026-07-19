// Project.h
//
// Project is the root of the entire object graph the spec describes:
// "Project stores Width, Height, FPS, Duration, Timeline, Tracks, Assets."
// EditorEngine (engine/EditorEngine.h) holds exactly one Project at a time
// and is the only class that mutates it -- and only ever via Commands, so
// undo/redo works uniformly across every kind of edit.
//
// Serialization (Create/Open/Rename/Duplicate/Delete project) is
// deliberately kept out of this header: Project is a pure in-memory model,
// and ProjectStore (not shown in Phase 1 scaffold, straightforward JSON
// serialization of this tree) is a separate class that reads/writes it.
// This separation means Project's structure can be unit-tested without
// touching the filesystem.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "core/MediaSource.h"
#include "core/Timeline.h"
#include "nle/core/Types.h"

namespace nle {

struct ProjectSettings {
    int width = 1080;
    int height = 1920;   // default portrait canvas, matches 9:16 short-form
    double fps = 30.0;
};

class Project {
public:
    Project(ProjectId id, std::string name, ProjectSettings settings)
        : id_(id), name_(std::move(name)), settings_(settings) {}

    ProjectId Id() const { return id_; }
    const std::string& Name() const { return name_; }
    void Rename(std::string newName) { name_ = std::move(newName); }

    const ProjectSettings& Settings() const { return settings_; }
    void SetSettings(ProjectSettings s) { settings_ = s; }

    Timeline& GetTimeline() { return timeline_; }
    const Timeline& GetTimeline() const { return timeline_; }

    MediaSource* ImportMedia(const std::string& uri, MediaType type) {
        auto source = std::make_unique<MediaSource>(MediaSourceId::Generate(), uri, type);
        MediaSource* ptr = source.get();
        mediaLibrary_[ptr->Id().value] = std::move(source);
        return ptr;
    }

    MediaSource* FindMedia(MediaSourceId id) const {
        auto it = mediaLibrary_.find(id.value);
        return it != mediaLibrary_.end() ? it->second.get() : nullptr;
    }

    const std::unordered_map<uint64_t, std::unique_ptr<MediaSource>>& MediaLibrary() const {
        return mediaLibrary_;
    }

private:
    ProjectId id_;
    std::string name_;
    ProjectSettings settings_;
    Timeline timeline_;
    std::unordered_map<uint64_t, std::unique_ptr<MediaSource>> mediaLibrary_;
};

}  // namespace nle
