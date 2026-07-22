#!/usr/bin/env bash
# Builds and runs the Phase 2 host test suite with a plain host g++ -- no
# NDK, no Android SDK, no GPU required. See effect_engine_tests.cpp's header
# comment for exactly what this does and doesn't verify.
set -euo pipefail
cd "$(dirname "$0")"

CPP_ROOT="../app/src/main/cpp"
ASSETS_ROOT="../app/src/main/assets/effects"

g++ -std=c++20 -Wall -Wextra \
    -I "$CPP_ROOT" -I "$CPP_ROOT/include" -I . \
    effect_engine_tests.cpp \
    stubs/Texture_host_stub.cpp \
    "$CPP_ROOT/util/Json.cpp" \
    "$CPP_ROOT/effects/EffectPackageLoader.cpp" \
    "$CPP_ROOT/effects/EffectNodeRegistry.cpp" \
    "$CPP_ROOT/effects/EffectRuntime.cpp" \
    -o run_tests

./run_tests "$ASSETS_ROOT"
