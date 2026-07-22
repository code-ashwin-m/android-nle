// EffectPackageLoader.h
//
// Parses the four JSON files that make up an .effect package (manifest.json,
// graph.json, parameters.json, animations.json -- see
// docs/ARCHITECTURE.md's "Effect Package Format" section and
// assets/effects/Dreamy.effect/ for a worked example) into an EffectGraphDef.
//
// Two entry points are provided deliberately:
//   - LoadFromDirectory: the real path, used by EffectRuntime on-device,
//     reads the four files from an unpacked package directory (or, later, a
//     content:// URI copied to app-private storage -- file access is
//     Android's job, not this class's).
//   - LoadFromJsonStrings: takes the four documents as strings directly.
//     This exists so EffectRuntime's logic -- and this loader's parsing
//     logic -- is unit-testable on a host machine with no filesystem/NDK
//     dependency at all (see host_tests/effect_runtime_test.cpp), which
//     matters specifically because this sandbox has no Android SDK/NDK to
//     validate a real on-device build against.

#pragma once

#include <string>

#include "effects/EffectGraphDef.h"

namespace nle {

class EffectPackageLoader {
public:
    static bool LoadFromDirectory(const std::string& packageDir, EffectGraphDef* outDef, std::string* error);

    static bool LoadFromJsonStrings(const std::string& manifestJson, const std::string& graphJson,
                                     const std::string& parametersJson, const std::string& animationsJson,
                                     EffectGraphDef* outDef, std::string* error);
};

}  // namespace nle
