// PropertyBag.h
//
// A named collection of heterogeneously-typed Property<T> instances. This is
// what a packaged Effect's declared parameters live in (see
// effects/EffectGraphDef.h, EffectRuntime::Instantiate) -- "Blur Radius" is a
// ScalarProperty, "Tint" is a ColorProperty, both live in the same bag, and
// nothing in this file knows or cares which effect they belong to.
//
// This exists alongside the existing per-EffectType `properties_` vector on
// Clip::Effect (Clip.h), not as a replacement for it: Brightness (Phase 1's
// only shipped effect) keeps using Effect::AddProperty/FindProperty exactly
// as before -- see this file's own header comment in Clip.h for why that
// path is left untouched. PropertyBag is additive, used only by the new
// packaged-effect path (Clip::Effect::Parameters()).
//
// Why std::variant instead of a base-class/virtual-Property hierarchy: a
// virtual base would need a type-erased ValueAt() returning some boxed
// Any-like value, which every caller then has to unbox anyway. std::variant
// keeps the concrete Property<T> type recoverable via std::get_if, so a node
// binding to "radius" gets back an actual Property<double>*, not a wrapper.

#pragma once

#include <string>
#include <variant>
#include <vector>

#include "core/Property.h"

namespace nle {

using AnyProperty = std::variant<ScalarProperty, Vec2Property, Vec3Property, ColorProperty>;

class PropertyBag {
public:
    ScalarProperty& AddScalar(const std::string& name, double defaultValue, PropertyMetadata meta = {}) {
        properties_.emplace_back(ScalarProperty(name, defaultValue, std::move(meta)));
        return std::get<ScalarProperty>(properties_.back());
    }
    Vec2Property& AddVec2(const std::string& name, Vec2 defaultValue, PropertyMetadata meta = {}) {
        properties_.emplace_back(Vec2Property(name, defaultValue, std::move(meta)));
        return std::get<Vec2Property>(properties_.back());
    }
    Vec3Property& AddVec3(const std::string& name, Vec3 defaultValue, PropertyMetadata meta = {}) {
        properties_.emplace_back(Vec3Property(name, defaultValue, std::move(meta)));
        return std::get<Vec3Property>(properties_.back());
    }
    ColorProperty& AddColor(const std::string& name, Color defaultValue, PropertyMetadata meta = {}) {
        properties_.emplace_back(ColorProperty(name, defaultValue, std::move(meta)));
        return std::get<ColorProperty>(properties_.back());
    }

    // Returns nullptr if `name` doesn't exist or exists with a different T --
    // callers that don't know the type ahead of time should use Find() +
    // std::visit instead.
    template <typename T>
    Property<T>* FindTyped(const std::string& name) {
        for (auto& p : properties_) {
            if (auto* typed = std::get_if<Property<T>>(&p)) {
                if (typed->Name() == name) return typed;
            }
        }
        return nullptr;
    }

    AnyProperty* Find(const std::string& name) {
        for (auto& p : properties_) {
            if (NameOf(p) == name) return &p;
        }
        return nullptr;
    }

    const std::vector<AnyProperty>& All() const { return properties_; }
    size_t Count() const { return properties_.size(); }

private:
    static std::string NameOf(const AnyProperty& p) {
        return std::visit([](auto&& prop) -> std::string { return prop.Name(); }, p);
    }

    // Pointers returned by AddScalar/AddVec2/.../FindTyped stay valid only as
    // long as this vector doesn't reallocate. EffectRuntime::Instantiate
    // (the only place that both populates a bag and hands out pointers into
    // it to nodes) always fully populates the bag *before* resolving any
    // node parameter against it, so no pointer is taken before the last
    // insertion -- see EffectRuntime.cpp. If a future feature needs to add
    // parameters to an already-wired bag, switch this to
    // std::vector<std::unique_ptr<AnyProperty>> first.
    std::vector<AnyProperty> properties_;
};

}  // namespace nle
