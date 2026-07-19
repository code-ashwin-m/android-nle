// Command.h
//
// "Every modification should become a command" (spec). Concretely: every
// JNI entry point that mutates project state (AddClip, DeleteClip,
// SetBrightness, Split, Trim, ...) constructs a Command subclass and hands
// it to CommandStack::Execute, rather than mutating Project directly. This
// is what makes Undo/Redo uniform instead of a hand-maintained list of
// "how to reverse each operation" scattered across the codebase.
//
// Commands mutate through Project* rather than capturing a Clip* etc.
// directly, and store IDs rather than pointers, because pointers into the
// project tree can be invalidated by *other* commands (e.g. deleting a
// track invalidates pointers to its clips) -- looking the target back up
// by ID inside Do()/Undo() is what keeps a Command safe to keep on the
// stack indefinitely, including across other users' edits in a future
// collaborative-editing feature.

#pragma once

#include <string>

namespace nle {

class Project;

class Command {
public:
    virtual ~Command() = default;

    // Applies the change. Called once when the command is first executed,
    // and again on Redo.
    virtual void Do(Project& project) = 0;

    // Reverses exactly what Do() did. Implementations should capture
    // whatever prior state they need *inside* Do(), not in the constructor,
    // since the constructor may run before the project is in the state the
    // command expects (e.g. commands queued from the UI thread ahead of
    // execution on the engine thread).
    virtual void Undo(Project& project) = 0;

    // Human-readable label for a future Edit History panel / accessibility.
    virtual std::string Description() const = 0;

    // Two consecutive commands of the same kind targeting the same object
    // (e.g. dragging a brightness slider, which fires SetBrightness dozens
    // of times a second) can coalesce into one undo step. Returning true
    // tells CommandStack to merge `next` into `this` instead of pushing a
    // new stack entry. Most commands don't support this and use the
    // default.
    virtual bool CanMergeWith(const Command& /*next*/) const { return false; }
    virtual void MergeWith(const Command& /*next*/) {}
};

}  // namespace nle
