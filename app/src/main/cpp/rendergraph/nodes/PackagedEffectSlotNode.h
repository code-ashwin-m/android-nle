// PackagedEffectSlotNode.h
//
// This is the node that actually connects the Effect Runtime to the main
// RenderGraph -- the "Effect Runtime -> Graph Builder -> RenderGraph" stages
// in docs/ARCHITECTURE.md's pipeline, made concrete.
//
// Same dynamic-lookup shape as BrightnessNode/ClipTransformNode (one
// instance per video track, looks up the active clip fresh every frame) --
// see ClipTransformNode.h's header comment for why that shape is required.
// The difference here is *what* varies between clips: Brightness/Transform
// vary only their parameter *values* per clip, but two clips can have
// completely different packaged effects with different node graphs
// entirely (Clip A: Dreamy = Color->Blur; Clip B: Cyberpunk = Color-
// >Transform; Clip C: no packaged effect at all). That's a topology
// difference, not just a value difference, and the outer RenderGraph's
// topology is only allowed to change on structural edits -- so this node
// owns and lazily builds one small embedded RenderGraph *per Effect
// instance* (cached by EffectId, for the Effect's whole lifetime), and
// executes whichever one matches the currently active clip's effect(s) as
// a self-contained sub-pipeline. The outer graph's shape never changes when
// the playhead crosses a cut between differently-effected clips; only which
// cached inner graph gets executed does.
//
// This is also exactly the seam the future standalone Effect Builder
// targets (spec: "The builder must use EXACTLY the same runtime"): the
// builder's live preview is this same EffectRuntime::Instantiate call
// against a single fixed PropertyBag, minus the "look up the active clip"
// step -- see docs/ARCHITECTURE.md, "Effect Builder compatibility".

#pragma once

#include <memory>
#include <unordered_map>

#include "core/Project.h"
#include "core/Track.h"
#include "effects/EffectRuntime.h"
#include "effects/nodes/ExternalInputNode.h"
#include "rendergraph/RenderGraph.h"
#include "rendergraph/RenderNode.h"

namespace nle {

class PackagedEffectSlotNode : public RenderNode {
public:
    PackagedEffectSlotNode(Project* project, TrackId trackId, EffectRuntime* runtime)
        : project_(project), trackId_(trackId), runtime_(runtime) {}

    std::string Name() const override { return "PackagedEffectSlot"; }

    void OnAttach(const RenderContext& context) override { attachContext_ = context; }

    void OnDetach() override {
        // Every cached inner graph holds GL resources (compiled shaders,
        // via each node's own OnAttach) tied to the GL context that's going
        // away -- see PlaybackEngine::DetachPreviewSurface, which calls
        // DetachAll() across the whole outer graph including this node.
        // Clearing the cache here means a future re-attach rebuilds and
        // recompiles cleanly, matching how every other node in this engine
        // already treats OnDetach as "the GL context is gone now."
        compiledByEffect_.clear();
    }

    Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) override {
        if (inputs.empty() || !inputs[0].IsValid()) return inputs.empty() ? Frame{} : inputs[0];

        Track* track = project_->GetTimeline().FindTrack(trackId_);
        Clip* clip = track ? track->ClipAt(context.time) : nullptr;
        if (!clip) return inputs[0];

        Frame current = inputs[0];
        for (auto& effectPtr : clip->Effects()) {
            Effect* effect = effectPtr.get();
            if (!effect->IsPackaged()) continue;
            Compiled* compiled = GetOrBuildCompiled(effect, context);
            if (!compiled) continue;  // package failed to load / isn't registered -- pass through unchanged
            compiled->inputNode->SetFrame(current);
            current = compiled->graph.Execute(context);
        }
        return current;
    }

private:
    struct Compiled {
        RenderGraph graph;
        ExternalInputNode* inputNode = nullptr;  // non-owning: owned by `graph`
    };

    Compiled* GetOrBuildCompiled(Effect* effect, const RenderContext& context) {
        uint64_t key = effect->Id().value;
        auto it = compiledByEffect_.find(key);
        if (it != compiledByEffect_.end()) return it->second.get();

        const EffectGraphDef* def = runtime_->FindLoaded(effect->PackageId());
        if (!def) return nullptr;

        auto compiled = std::make_unique<Compiled>();
        auto inputNode = std::make_unique<ExternalInputNode>();
        ExternalInputNode* inputPtr = inputNode.get();
        NodeHandle inputHandle = compiled->graph.AddNode(std::move(inputNode));

        NodeHandle outputHandle = runtime_->Instantiate(*def, effect->Parameters(), compiled->graph, inputHandle);
        compiled->graph.SetOutputNode(outputHandle);
        compiled->graph.AttachAll(context);
        compiled->inputNode = inputPtr;

        auto insertion = compiledByEffect_.emplace(key, std::move(compiled));
        return insertion.first->second.get();
    }

    Project* project_;
    TrackId trackId_;
    EffectRuntime* runtime_;
    RenderContext attachContext_;
    std::unordered_map<uint64_t, std::unique_ptr<Compiled>> compiledByEffect_;
};

}  // namespace nle
