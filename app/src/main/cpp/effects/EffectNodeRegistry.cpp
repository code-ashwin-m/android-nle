#include "effects/EffectNodeRegistry.h"

#include "effects/nodes/BlurNode.h"
#include "effects/nodes/ColorNode.h"
#include "effects/nodes/TransformNode.h"

namespace nle {

EffectNodeRegistry& EffectNodeRegistry::Instance() {
    static EffectNodeRegistry instance;
    return instance;
}

EffectNodeRegistry::EffectNodeRegistry() {
    Register("ColorNode", [](const NodeInstanceDef& def, PropertyBag& bag) -> std::unique_ptr<RenderNode> {
        return std::make_unique<ColorNode>(def, bag);
    });
    Register("TransformNode", [](const NodeInstanceDef& def, PropertyBag& bag) -> std::unique_ptr<RenderNode> {
        return std::make_unique<TransformNode>(def, bag);
    });
    Register("BlurNode", [](const NodeInstanceDef& def, PropertyBag& bag) -> std::unique_ptr<RenderNode> {
        return std::make_unique<BlurNode>(def, bag);
    });

    // Declared in the spec's node list, not yet implemented in Phase 2:
    // GlowNode, OverlayNode, NoiseNode, MaskNode, LUTNode, ParticleNode.
    // Each follows the exact shape of BlurNode/ColorNode/TransformNode --
    // a RenderNode subclass in effects/nodes/, constructed from
    // (NodeInstanceDef, PropertyBag&), registered here with one more
    // Register() call. See docs/ARCHITECTURE.md, "Node types not yet
    // implemented" for the specific shader/approach sketched for each.
}

void EffectNodeRegistry::Register(const std::string& typeName, Factory factory) {
    factories_[typeName] = std::move(factory);
}

bool EffectNodeRegistry::IsRegistered(const std::string& typeName) const {
    return factories_.find(typeName) != factories_.end();
}

std::unique_ptr<RenderNode> EffectNodeRegistry::Create(const NodeInstanceDef& def, PropertyBag& bag) const {
    auto it = factories_.find(def.type);
    if (it == factories_.end()) return nullptr;
    return it->second(def, bag);
}

}  // namespace nle
