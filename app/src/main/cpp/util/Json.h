// Json.h
//
// EditorEngine::GetProjectSnapshotJson (engine/EditorEngine.cpp) already
// established this codebase's position on JSON: a hand-rolled implementation
// is simpler than adding a dependency at this data volume, and switching to
// a real library later is a contained, one-file change. That file only ever
// *writes* JSON, though -- loading .effect packages (see
// effects/EffectPackageLoader.h) is the first thing that needs to *read* it,
// so this is that reader, kept to the same one-file-dependency philosophy.
//
// Deliberately supports the full JSON value model (object/array/string/
// number/bool/null) since effect package files are hand-authored/tooled
// data, not a fixed schema this parser can shortcut -- unlike the snapshot
// writer, which only ever emits the exact shape EditorState.kt expects.

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace nle {

enum class JsonType { Null, Bool, Number, String, Array, Object };

class JsonValue {
public:
    JsonValue() = default;

    static JsonValue MakeNull() { return JsonValue(); }

    JsonType Type() const { return type_; }
    bool IsNull() const { return type_ == JsonType::Null; }

    bool AsBool(bool fallback = false) const { return type_ == JsonType::Bool ? bool_ : fallback; }
    double AsNumber(double fallback = 0.0) const { return type_ == JsonType::Number ? number_ : fallback; }
    const std::string& AsString(const std::string& fallback = "") const {
        return type_ == JsonType::String ? string_ : fallback;
    }
    const std::vector<JsonValue>& AsArray() const { return array_; }
    const std::vector<std::pair<std::string, JsonValue>>& AsObject() const { return object_; }

    // Object accessors. Get() returns a pointer to a static Null instance
    // (never nullptr) when the key is absent, so call sites can chain
    // `.Get("a").Get("b").AsNumber(0)` without a null check at every step --
    // exactly the shape effect-package parsing code needs, since most
    // fields are optional with a sensible default.
    const JsonValue& Get(const std::string& key) const;
    bool Has(const std::string& key) const;

    static void Set(JsonValue& v, JsonType t) { v.type_ = t; }

    // Parses `text`. On failure returns a Null JsonValue and, if `error` is
    // non-null, writes a human-readable message (with byte offset) into it.
    // Never throws -- effect package loading is expected to fail on
    // malformed/hand-edited user or third-party content, not crash the
    // engine.
    static JsonValue Parse(const std::string& text, std::string* error = nullptr);

private:
    friend class JsonParser;

    JsonType type_ = JsonType::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<std::pair<std::string, JsonValue>> object_;
};

}  // namespace nle
