// EffectNodeRegistry.h
//
// "Effects are combinations of nodes" (spec). This is where a graph.json
// node's `"type": "ColorNode"` string turns into an actual C++ object. It is
// the ONLY place in the engine that maps a node type name to a constructor
// -- EffectRuntime::Instantiate calls Create() once per NodeInstanceDef and
// never itself knows what a "ColorNode" is.
//
// Registering a new BUILT-IN node type (e.g. a future GlowNode) means
// writing effects/nodes/GlowNode.h and adding one Register() call in this
// file's .cpp -- never touching RenderGraph, RenderNode, EffectRuntime, or
// any existing node. Adding a new EFFECT that only recombines existing node
// types (see assets/effects/Cyberpunk.effect/ vs Dreamy.effect/) touches
// NOTHING in this file, or anywhere else in C++ -- that's the "add a second
// effect without changing C++" demonstration.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "effects/EffectGraphDef.h"

namespace nle {

class RenderNode;
class PropertyBag;

class EffectNodeRegistry {
public:
    using Factory = std::function<std::unique_ptr<RenderNode>(const NodeInstanceDef&, PropertyBag&)>;

    static EffectNodeRegistry& Instance();

    void Register(const std::string& typeName, Factory factory);
    bool IsRegistered(const std::string& typeName) const;

    // Returns nullptr if `def.type` isn't registered -- callers (see
    // EffectRuntime::Instantiate) skip that one node rather than failing an
    // entire effect instantiation, so a package built against a newer
    // runtime with a node type this version doesn't have yet degrades to
    // "missing that one node" instead of "effect doesn't load at all."
    std::unique_ptr<RenderNode> Create(const NodeInstanceDef& def, PropertyBag& bag) const;

private:
    EffectNodeRegistry();  // registers every built-in node type

    std::unordered_map<std::string, Factory> factories_;
};

}  // namespace nle
