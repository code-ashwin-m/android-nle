#include "effects/EffectPackageLoader.h"

#include <fstream>
#include <sstream>

#include "util/Json.h"

namespace nle {

namespace {

bool ReadFile(const std::string& path, std::string* outText) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    *outText = buffer.str();
    return true;
}

InterpolationType ParseInterpolation(const std::string& s) {
    if (s == "hold") return InterpolationType::Hold;
    if (s == "bezier") return InterpolationType::Bezier;
    if (s == "easeIn") return InterpolationType::EaseIn;
    if (s == "easeOut") return InterpolationType::EaseOut;
    if (s == "easeInOut") return InterpolationType::EaseInOut;
    return InterpolationType::Linear;  // also covers "linear" and unrecognized values
}

Vec2 ParseVec2(const JsonValue& v) { return Vec2{v.Get("x").AsNumber(0.0), v.Get("y").AsNumber(0.0)}; }

Vec3 ParseVec3(const JsonValue& v) {
    return Vec3{v.Get("x").AsNumber(0.0), v.Get("y").AsNumber(0.0), v.Get("z").AsNumber(0.0)};
}

Color ParseColor(const JsonValue& v) {
    return Color{v.Get("r").AsNumber(0.0), v.Get("g").AsNumber(0.0), v.Get("b").AsNumber(0.0),
                 v.Get("a").AsNumber(1.0)};
}

bool ParseManifest(const std::string& json, EffectManifest* out, std::string* error) {
    JsonValue root = JsonValue::Parse(json, error);
    if (root.IsNull() && error && !error->empty()) return false;
    out->id = root.Get("id").AsString();
    out->name = root.Get("name").AsString();
    out->category = root.Get("category").AsString("Uncategorized");
    out->author = root.Get("author").AsString();
    out->formatVersion = static_cast<int>(root.Get("formatVersion").AsNumber(1));
    if (out->id.empty()) {
        if (error) *error = "manifest.json: missing required field 'id'";
        return false;
    }
    return true;
}

// A param value in graph.json is one of:
//   5.0                          -- literal float
//   {"x": 1, "y": 2}             -- literal vec2
//   {"r":1,"g":1,"b":1,"a":1}    -- literal color
//   {"ref": "exposure"}          -- bound to a declared effect parameter
NodeParamBinding ParseParamBinding(const std::string& paramName, const JsonValue& value) {
    NodeParamBinding binding;
    binding.paramName = paramName;
    if (value.Type() == JsonType::Object && value.Has("ref")) {
        binding.isReference = true;
        binding.referenceName = value.Get("ref").AsString();
        return binding;
    }
    if (value.Type() == JsonType::Number) {
        binding.literalType = "float";
        binding.literalScalar = value.AsNumber(0.0);
        return binding;
    }
    if (value.Type() == JsonType::Object && value.Has("r")) {
        binding.literalType = "color";
        binding.literalColor = ParseColor(value);
        return binding;
    }
    if (value.Type() == JsonType::Object && value.Has("x")) {
        binding.literalType = "vec2";
        binding.literalVec2 = ParseVec2(value);
        return binding;
    }
    // Unrecognized shape: fall back to a zero float literal rather than
    // failing the whole package load over one malformed parameter -- the
    // effect will render with that param at zero, which is recoverable and
    // visible, instead of the clip losing its whole effect stack.
    binding.literalType = "float";
    binding.literalScalar = 0.0;
    return binding;
}

bool ParseGraph(const std::string& json, EffectGraphDef* out, std::string* error) {
    JsonValue root = JsonValue::Parse(json, error);
    if (root.IsNull() && error && !error->empty()) return false;

    for (const JsonValue& nodeJson : root.Get("nodes").AsArray()) {
        NodeInstanceDef node;
        node.id = nodeJson.Get("id").AsString();
        node.type = nodeJson.Get("type").AsString();
        if (node.id.empty() || node.type.empty()) {
            if (error) *error = "graph.json: every node needs a non-empty 'id' and 'type'";
            return false;
        }
        const JsonValue& params = nodeJson.Get("params");
        if (params.Type() == JsonType::Object) {
            for (auto& [name, value] : params.AsObject()) {
                node.params.push_back(ParseParamBinding(name, value));
            }
        }
        out->nodes.push_back(std::move(node));
    }

    for (const JsonValue& connJson : root.Get("connections").AsArray()) {
        ConnectionDef conn;
        conn.from = connJson.Get("from").AsString();
        conn.to = connJson.Get("to").AsString();
        if (conn.from.empty() || conn.to.empty()) {
            if (error) *error = "graph.json: every connection needs a non-empty 'from' and 'to'";
            return false;
        }
        out->connections.push_back(std::move(conn));
    }
    return true;
}

bool ParseParameters(const std::string& json, EffectGraphDef* out, std::string* error) {
    JsonValue root = JsonValue::Parse(json, error);
    if (root.IsNull() && error && !error->empty()) return false;

    for (const JsonValue& p : root.AsArray()) {
        EffectParameterDef def;
        def.name = p.Get("name").AsString();
        def.type = p.Get("type").AsString("float");
        if (def.name.empty()) {
            if (error) *error = "parameters.json: every parameter needs a non-empty 'name'";
            return false;
        }
        const JsonValue& defaultValue = p.Get("default");
        if (def.type == "vec2") {
            def.defaultVec2 = ParseVec2(defaultValue);
        } else if (def.type == "vec3") {
            def.defaultVec3 = ParseVec3(defaultValue);
        } else if (def.type == "color") {
            def.defaultColor = ParseColor(defaultValue);
        } else {
            def.defaultScalar = defaultValue.AsNumber(0.0);
        }
        def.metadata.category = p.Get("category").AsString("Effect");
        def.metadata.uiHint = p.Get("uiHint").AsString("slider");
        if (p.Has("min") && p.Has("max")) {
            def.metadata.hasRange = true;
            def.metadata.minValue = p.Get("min").AsNumber(0.0);
            def.metadata.maxValue = p.Get("max").AsNumber(1.0);
        }
        out->parameters.push_back(std::move(def));
    }
    return true;
}

bool ParseAnimations(const std::string& json, EffectGraphDef* out, std::string* error) {
    if (json.empty()) return true;  // animations.json is optional -- not every effect ships a default animation
    JsonValue root = JsonValue::Parse(json, error);
    if (root.IsNull() && error && !error->empty()) return false;

    for (const JsonValue& animJson : root.AsArray()) {
        EffectAnimationDef anim;
        anim.parameter = animJson.Get("parameter").AsString();
        const EffectParameterDef* paramDef = out->FindParameter(anim.parameter);
        if (!paramDef) {
            if (error) *error = "animations.json: references unknown parameter '" + anim.parameter + "'";
            return false;
        }
        for (const JsonValue& kfJson : animJson.Get("keyframes").AsArray()) {
            EffectKeyframeDef kf;
            kf.time = SecondsToUs(kfJson.Get("timeSeconds").AsNumber(0.0));
            kf.interpolation = ParseInterpolation(kfJson.Get("interpolation").AsString("linear"));
            const JsonValue& value = kfJson.Get("value");
            if (paramDef->type == "vec2") {
                Vec2 v = ParseVec2(value);
                kf.v0 = v.x;
                kf.v1 = v.y;
            } else if (paramDef->type == "vec3") {
                Vec3 v = ParseVec3(value);
                kf.v0 = v.x;
                kf.v1 = v.y;
                kf.v2 = v.z;
            } else if (paramDef->type == "color") {
                Color c = ParseColor(value);
                kf.v0 = c.r;
                kf.v1 = c.g;
                kf.v2 = c.b;
                kf.v3 = c.a;
            } else {
                kf.v0 = value.AsNumber(0.0);
            }
            anim.keyframes.push_back(kf);
        }
        out->defaultAnimations.push_back(std::move(anim));
    }
    return true;
}

}  // namespace

bool EffectPackageLoader::LoadFromDirectory(const std::string& packageDir, EffectGraphDef* outDef,
                                             std::string* error) {
    std::string manifestJson, graphJson, parametersJson, animationsJson;
    if (!ReadFile(packageDir + "/manifest.json", &manifestJson)) {
        if (error) *error = "missing manifest.json in " + packageDir;
        return false;
    }
    if (!ReadFile(packageDir + "/graph.json", &graphJson)) {
        if (error) *error = "missing graph.json in " + packageDir;
        return false;
    }
    if (!ReadFile(packageDir + "/parameters.json", &parametersJson)) {
        if (error) *error = "missing parameters.json in " + packageDir;
        return false;
    }
    ReadFile(packageDir + "/animations.json", &animationsJson);  // optional; empty string if absent
    return LoadFromJsonStrings(manifestJson, graphJson, parametersJson, animationsJson, outDef, error);
}

bool EffectPackageLoader::LoadFromJsonStrings(const std::string& manifestJson, const std::string& graphJson,
                                               const std::string& parametersJson, const std::string& animationsJson,
                                               EffectGraphDef* outDef, std::string* error) {
    *outDef = EffectGraphDef{};
    if (!ParseManifest(manifestJson, &outDef->manifest, error)) return false;
    if (outDef->manifest.formatVersion > 1) {
        // Package versioning (spec: "Version the package. Support future
        // backward compatibility."). Phase 2 only defines format version 1;
        // a newer package asking for a version this runtime doesn't
        // understand fails closed with a clear reason instead of silently
        // misinterpreting fields a future version might repurpose.
        if (error) *error = "package requires format version " + std::to_string(outDef->manifest.formatVersion) +
                             ", this runtime supports up to 1";
        return false;
    }
    if (!ParseParameters(parametersJson, outDef, error)) return false;
    if (!ParseGraph(graphJson, outDef, error)) return false;
    if (!ParseAnimations(animationsJson, outDef, error)) return false;
    return true;
}

}  // namespace nle
