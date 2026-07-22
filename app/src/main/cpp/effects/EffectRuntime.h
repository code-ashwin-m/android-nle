// EffectRuntime.h
//
// This is the "Effect Runtime" + "Graph Builder" stages from the pipeline
// in docs/ARCHITECTURE.md:
//
//   Playback Clock -> Timeline Evaluator -> Property Evaluation ->
//   Effect Runtime -> Graph Builder -> RenderGraph -> Renderer
//
// PlaybackEngine::RebuildGraphIfNeeded (Timeline Evaluator) already walks
// every clip once per structural change; for each clip effect that's
// package-backed (Clip::Effect::PackageId() is non-empty -- see Clip.h),
// it calls EffectRuntime::Instantiate here, which:
//   1. populates that Effect's PropertyBag from the package's declared
//      parameters (Property Evaluation's *source of truth* -- the runtime
//      per-frame evaluation itself still happens inside each node's
//      Process(), exactly like BrightnessNode always has, via
//      NodeParam<T>::ValueAt(time))
//   2. builds one RenderNode per NodeInstanceDef via EffectNodeRegistry
//   3. wires them into the existing RenderGraph via the graph's own public
//      AddNode/Connect (Graph Builder -- no RenderGraph API changes needed)
//
// The engine (RenderGraph, RenderNode, RenderContext) never knows any of
// this happened -- it just executes a topologically-sorted list of nodes
// the way it always has.

#pragma once

#include <string>
#include <unordered_map>

#include "core/PropertyBag.h"
#include "effects/EffectGraphDef.h"
#include "rendergraph/RenderGraph.h"

namespace nle {

class EffectRuntime {
public:
    // Loads and parses a package from an unpacked directory, caching the
    // result by directory path (the "Graph Cache" from the spec's
    // performance requirements -- re-adding the same effect to a second
    // clip does not re-parse JSON). Returns nullptr and sets *error on
    // failure; the package directory is untouched on disk either way.
    const EffectGraphDef* LoadPackage(const std::string& packageDir, std::string* error = nullptr);

    // Host-testable / in-memory seam: registers an already-parsed
    // definition directly, keyed by its own manifest.id, with no filesystem
    // access. Real device code uses LoadPackage; host_tests/ and any future
    // "bundled built-in effects" path can use this directly.
    const EffectGraphDef* RegisterPackage(EffectGraphDef def);

    const EffectGraphDef* FindLoaded(const std::string& packageId) const;

    // Populates `bagOut` from `def`'s declared parameters and default
    // animations. Idempotent: a `bagOut` that already has entries in it is
    // left untouched, which is what makes it safe to call this both eagerly
    // (see EditorEngine::AddPackagedEffect, so a Properties Panel has
    // parameters to show the instant an effect is added, before any frame
    // has rendered) and lazily (from Instantiate() below, in case a bag
    // somehow reaches here unpopulated).
    void PopulateParameters(const EffectGraphDef& def, PropertyBag& bagOut) const;

    // Populates `bagOut` from `def` (if not already populated -- see
    // PopulateParameters), builds one RenderNode per NodeInstanceDef via
    // EffectNodeRegistry, wires them into `graph` per `def.connections`
    // (with the reserved "input" id bound to `inputHandle`), and returns
    // the subgraph's output NodeHandle -- callers Connect() this onward
    // exactly like any other node handle. `bagOut` must outlive every node
    // this call creates (see core/PropertyBag.h's header comment on
    // pointer stability) -- callers own it, typically as a member of the
    // target Clip::Effect.
    NodeHandle Instantiate(const EffectGraphDef& def, PropertyBag& bagOut, RenderGraph& graph,
                            NodeHandle inputHandle) const;

private:
    std::unordered_map<std::string, EffectGraphDef> byDirectory_;
    std::unordered_map<std::string, EffectGraphDef> byPackageId_;
};

}  // namespace nle
