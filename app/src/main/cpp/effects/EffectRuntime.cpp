#include "effects/EffectRuntime.h"

#include "effects/EffectNodeRegistry.h"
#include "effects/EffectPackageLoader.h"

namespace nle {

const EffectGraphDef* EffectRuntime::LoadPackage(const std::string& packageDir, std::string* error) {
    auto cached = byDirectory_.find(packageDir);
    if (cached != byDirectory_.end()) return &cached->second;

    EffectGraphDef def;
    if (!EffectPackageLoader::LoadFromDirectory(packageDir, &def, error)) return nullptr;

    auto [it, inserted] = byDirectory_.emplace(packageDir, std::move(def));
    byPackageId_[it->second.manifest.id] = it->second;
    (void)inserted;
    return &it->second;
}

const EffectGraphDef* EffectRuntime::RegisterPackage(EffectGraphDef def) {
    std::string id = def.manifest.id;
    auto [it, inserted] = byPackageId_.insert_or_assign(id, std::move(def));
    (void)inserted;
    return &it->second;
}

const EffectGraphDef* EffectRuntime::FindLoaded(const std::string& packageId) const {
    auto it = byPackageId_.find(packageId);
    return it != byPackageId_.end() ? &it->second : nullptr;
}

void EffectRuntime::PopulateParameters(const EffectGraphDef& def, PropertyBag& bagOut) const {
    if (bagOut.Count() > 0) return;  // already populated -- see header comment on why this must be idempotent

    for (const EffectParameterDef& p : def.parameters) {
        if (p.type == "vec2") {
            bagOut.AddVec2(p.name, p.defaultVec2, p.metadata);
        } else if (p.type == "vec3") {
            bagOut.AddVec3(p.name, p.defaultVec3, p.metadata);
        } else if (p.type == "color") {
            bagOut.AddColor(p.name, p.defaultColor, p.metadata);
        } else {
            bagOut.AddScalar(p.name, p.defaultScalar, p.metadata);
        }
    }

    for (const EffectAnimationDef& anim : def.defaultAnimations) {
        const EffectParameterDef* paramDef = def.FindParameter(anim.parameter);
        if (!paramDef) continue;
        if (paramDef->type == "vec2") {
            if (auto* prop = bagOut.FindTyped<Vec2>(anim.parameter)) {
                for (auto& kf : anim.keyframes) prop->AddKeyframe(kf.time, Vec2{kf.v0, kf.v1}, kf.interpolation);
            }
        } else if (paramDef->type == "vec3") {
            if (auto* prop = bagOut.FindTyped<Vec3>(anim.parameter)) {
                for (auto& kf : anim.keyframes) {
                    prop->AddKeyframe(kf.time, Vec3{kf.v0, kf.v1, kf.v2}, kf.interpolation);
                }
            }
        } else if (paramDef->type == "color") {
            if (auto* prop = bagOut.FindTyped<Color>(anim.parameter)) {
                for (auto& kf : anim.keyframes) {
                    prop->AddKeyframe(kf.time, Color{kf.v0, kf.v1, kf.v2, kf.v3}, kf.interpolation);
                }
            }
        } else {
            if (auto* prop = bagOut.FindTyped<double>(anim.parameter)) {
                for (auto& kf : anim.keyframes) prop->AddKeyframe(kf.time, kf.v0, kf.interpolation);
            }
        }
    }
}

NodeHandle EffectRuntime::Instantiate(const EffectGraphDef& def, PropertyBag& bagOut, RenderGraph& graph,
                                       NodeHandle inputHandle) const {
    // 1. Populate the target Effect's PropertyBag from the package's
    //    declared parameters, unless something (typically
    //    EditorEngine::AddPackagedEffect, at the moment the effect was
    //    added) already did. Must fully complete before any node is
    //    constructed -- see core/PropertyBag.h's header comment on why
    //    pointer stability depends on that ordering.
    PopulateParameters(def, bagOut);

    // 2. Build one RenderNode per NodeInstanceDef. An unregistered node type
    //    (package built against a newer runtime) is skipped, not fatal --
    //    see EffectNodeRegistry::Create's header comment.
    std::unordered_map<std::string, NodeHandle> handles;
    for (const NodeInstanceDef& nodeDef : def.nodes) {
        std::unique_ptr<RenderNode> node = EffectNodeRegistry::Instance().Create(nodeDef, bagOut);
        if (!node) continue;
        handles[nodeDef.id] = graph.AddNode(std::move(node));
    }

    // 3. Wire connections. "input" is a reserved id meaning `inputHandle`;
    //    "output" (as a `to`) marks which real node's handle this whole
    //    subgraph exposes upstream. A package with zero connections is a
    //    valid (if useless) pure pass-through -- outputHandle defaults to
    //    inputHandle so callers never need a special case.
    NodeHandle outputHandle = inputHandle;
    for (const ConnectionDef& conn : def.connections) {
        NodeHandle from = kInvalidNode;
        if (conn.from == "input") {
            from = inputHandle;
        } else if (auto it = handles.find(conn.from); it != handles.end()) {
            from = it->second;
        }

        if (conn.to == "output") {
            if (from != kInvalidNode) outputHandle = from;
            continue;
        }

        auto toIt = handles.find(conn.to);
        if (from == kInvalidNode || toIt == handles.end()) {
            // References a node id this package didn't define, or one whose
            // type wasn't registered -- skip this one edge rather than
            // aborting the whole splice; RenderGraph::Execute simply won't
            // reach whatever's downstream of the missing edge.
            continue;
        }
        graph.Connect(from, toIt->second);
    }
    return outputHandle;
}

}  // namespace nle
