// EffectGraphDef.h
//
// This is the in-memory shape of an .effect package (see
// docs/ARCHITECTURE.md, "Effect Package Format", and the example packages
// under assets/effects/). EffectPackageLoader.h parses manifest.json/
// graph.json/parameters.json/animations.json into one of these;
// EffectRuntime.h turns one of these plus a target Clip's PropertyBag into
// real RenderNode instances spliced into the shared RenderGraph.
//
// Every field here is plain data -- there is deliberately no behavior, no
// virtual dispatch, nothing effect-specific. "Dreamy" and "Cyberpunk" (see
// assets/effects/) produce structurally identical EffectGraphDef instances;
// only the field *values* differ. That's the whole point: the C++ type
// system never encodes "this is the Dreamy effect" anywhere.

#pragma once

#include <string>
#include <vector>

#include "core/MathTypes.h"
#include "core/Property.h"
#include "core/PropertyBag.h"
#include "nle/core/Types.h"

namespace nle {

// A node-local parameter that is either bound to an animatable Property<T>
// (looked up from the clip's PropertyBag by name at instantiation time) or a
// fixed literal baked into the graph definition itself. Every generic node
// (ColorNode, TransformNode, BlurNode, future GlowNode/NoiseNode/...) reads
// its inputs exclusively through NodeParam<T>::ValueAt(time) -- it never
// knows or cares which case it's in, which is what lets EffectRuntime freely
// choose per binding without the node needing two code paths.
template <typename T>
class NodeParam {
public:
    NodeParam() = default;
    static NodeParam Bound(Property<T>* prop) {
        NodeParam p;
        p.prop_ = prop;
        return p;
    }
    static NodeParam Literal(T value) {
        NodeParam p;
        p.literal_ = value;
        return p;
    }

    T ValueAt(TimeUs time) const { return prop_ ? prop_->ValueAt(time) : literal_; }
    bool IsBound() const { return prop_ != nullptr; }

private:
    Property<T>* prop_ = nullptr;
    T literal_{};
};

// One entry in a NodeInstanceDef's `params` map: either `{"ref": "<name>"}`
// (bound to a declared effect parameter -- animatable) or a literal value
// parsed directly from graph.json (fixed for the life of this effect
// instance -- e.g. a node's internal blend mode constant).
struct NodeParamBinding {
    std::string paramName;  // the node-local parameter name, e.g. "radius"
    bool isReference = false;
    std::string referenceName;  // valid iff isReference

    // Valid iff !isReference. literalType selects which field to read;
    // kept as a string ("float"/"vec2"/"color") rather than an enum because
    // it's set directly from the JSON value's shape during loading, not
    // from a schema this file would otherwise have to duplicate.
    std::string literalType = "float";
    double literalScalar = 0.0;
    Vec2 literalVec2;
    Color literalColor;
};

// One node instance in the graph, e.g. `{"id": "n1", "type": "ColorNode",
// "params": {...}}`. `type` is the key EffectNodeRegistry looks up to find
// the C++ constructor -- adding a new *instance* of an existing node type
// never touches C++; adding a new node *type* (a new built-in like GlowNode)
// does, exactly once, in effects/nodes/.
struct NodeInstanceDef {
    std::string id;
    std::string type;
    std::vector<NodeParamBinding> params;
};

// `{"from": "input", "to": "n1"}` etc. "input" and "output" are reserved
// node ids representing the splice boundary -- see EffectRuntime::Instantiate
// -- and never appear in `nodes`.
struct ConnectionDef {
    std::string from;
    std::string to;
};

// One entry in parameters.json: an effect parameter a user can see and
// keyframe in the Properties Panel (once that panel is wired to packaged
// effects -- see docs/ARCHITECTURE.md's noted UI seam). `type` selects which
// default* field is meaningful.
struct EffectParameterDef {
    std::string name;
    std::string type;  // "float" | "vec2" | "vec3" | "color"
    double defaultScalar = 0.0;
    Vec2 defaultVec2;
    Vec3 defaultVec3;
    Color defaultColor;
    PropertyMetadata metadata;
};

// One keyframe in animations.json. Values are carried generically as up to
// four doubles (interpreted per the owning EffectAnimationDef's parameter
// type) rather than as a variant, since this struct only ever exists
// transiently during loading -- EffectRuntime::Instantiate immediately
// converts it into a real Keyframe<T> on the correct Property<T>.
struct EffectKeyframeDef {
    TimeUs time = 0;
    InterpolationType interpolation = InterpolationType::Linear;
    double v0 = 0.0, v1 = 0.0, v2 = 0.0, v3 = 0.0;
};

// An optional default animation applied to one declared parameter the
// moment an effect instance is created -- what makes "Dreamy" visibly
// animate as soon as a user drags it onto a clip, rather than requiring
// them to hand-author keyframes for a preset effect to do anything.
struct EffectAnimationDef {
    std::string parameter;
    std::vector<EffectKeyframeDef> keyframes;
};

struct EffectManifest {
    std::string id;      // stable identifier, e.g. "com.nle.dreamy" -- referenced by Clip::Effect::PackageId()
    std::string name;     // display name, e.g. "Dreamy"
    std::string category;  // Effects panel grouping, e.g. "Look"
    std::string author;
    int formatVersion = 1;  // .effect package schema version -- see "Package versioning" in ARCHITECTURE.md
};

// Finds `paramName` among `node.params` and resolves it against `bag`
// (if bound) or its own literal (if not), falling back to `fallback` if the
// package didn't specify this parameter at all -- which keeps a node's
// constructor working even against a hand-edited/older package that's
// missing an optional parameter, rather than crashing.
inline NodeParam<double> ResolveScalarParam(const NodeInstanceDef& node, const std::string& paramName,
                                             PropertyBag& bag, double fallback = 0.0) {
    for (auto& binding : node.params) {
        if (binding.paramName != paramName) continue;
        if (binding.isReference) {
            if (auto* prop = bag.FindTyped<double>(binding.referenceName)) return NodeParam<double>::Bound(prop);
            return NodeParam<double>::Literal(fallback);
        }
        return NodeParam<double>::Literal(binding.literalScalar);
    }
    return NodeParam<double>::Literal(fallback);
}

inline NodeParam<Vec2> ResolveVec2Param(const NodeInstanceDef& node, const std::string& paramName, PropertyBag& bag,
                                         Vec2 fallback = {}) {
    for (auto& binding : node.params) {
        if (binding.paramName != paramName) continue;
        if (binding.isReference) {
            if (auto* prop = bag.FindTyped<Vec2>(binding.referenceName)) return NodeParam<Vec2>::Bound(prop);
            return NodeParam<Vec2>::Literal(fallback);
        }
        return NodeParam<Vec2>::Literal(binding.literalVec2);
    }
    return NodeParam<Vec2>::Literal(fallback);
}

inline NodeParam<Color> ResolveColorParam(const NodeInstanceDef& node, const std::string& paramName,
                                           PropertyBag& bag, Color fallback = {}) {
    for (auto& binding : node.params) {
        if (binding.paramName != paramName) continue;
        if (binding.isReference) {
            if (auto* prop = bag.FindTyped<Color>(binding.referenceName)) return NodeParam<Color>::Bound(prop);
            return NodeParam<Color>::Literal(fallback);
        }
        return NodeParam<Color>::Literal(binding.literalColor);
    }
    return NodeParam<Color>::Literal(fallback);
}

struct EffectGraphDef {
    EffectManifest manifest;
    std::vector<NodeInstanceDef> nodes;
    std::vector<ConnectionDef> connections;
    std::vector<EffectParameterDef> parameters;
    std::vector<EffectAnimationDef> defaultAnimations;

    const EffectParameterDef* FindParameter(const std::string& name) const {
        for (auto& p : parameters) {
            if (p.name == name) return &p;
        }
        return nullptr;
    }
};

}  // namespace nle
