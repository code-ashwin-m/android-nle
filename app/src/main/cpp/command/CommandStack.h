// CommandStack.h
//
// Owns the undo and redo stacks. Lives inside EditorEngine and is only ever
// touched from the engine's single command-processing thread (see
// engine/EditorEngine.h) -- so, deliberately, this class has no internal
// locking. Serializing all mutation through one thread is what lets every
// Command implementation assume it's never racing another command, which
// is far simpler and less error-prone than making every Command
// thread-safe individually.

#pragma once

#include <memory>
#include <vector>

#include "command/Command.h"

namespace nle {

class Project;

class CommandStack {
public:
    explicit CommandStack(size_t maxHistory = 200) : maxHistory_(maxHistory) {}

    void Execute(std::unique_ptr<Command> command, Project& project) {
        if (!undoStack_.empty() && undoStack_.back()->CanMergeWith(*command)) {
            undoStack_.back()->MergeWith(*command);
            undoStack_.back()->Do(project);
        } else {
            command->Do(project);
            undoStack_.push_back(std::move(command));
            if (undoStack_.size() > maxHistory_) {
                undoStack_.erase(undoStack_.begin());
            }
        }
        redoStack_.clear();  // any new edit invalidates the redo history
    }

    bool CanUndo() const { return !undoStack_.empty(); }
    bool CanRedo() const { return !redoStack_.empty(); }

    void Undo(Project& project) {
        if (undoStack_.empty()) return;
        std::unique_ptr<Command> cmd = std::move(undoStack_.back());
        undoStack_.pop_back();
        cmd->Undo(project);
        redoStack_.push_back(std::move(cmd));
    }

    void Redo(Project& project) {
        if (redoStack_.empty()) return;
        std::unique_ptr<Command> cmd = std::move(redoStack_.back());
        redoStack_.pop_back();
        cmd->Do(project);
        undoStack_.push_back(std::move(cmd));
    }

    void Clear() {
        undoStack_.clear();
        redoStack_.clear();
    }

private:
    size_t maxHistory_;
    std::vector<std::unique_ptr<Command>> undoStack_;
    std::vector<std::unique_ptr<Command>> redoStack_;
};

}  // namespace nle
