// Texture_host_stub.cpp
//
// This project's Android build (app/src/main/cpp/CMakeLists.txt) compiles
// render/Texture.cpp, whose CreateEmpty/~Texture call real GLES3 functions
// (glGenTextures, glDeleteTextures, glTexImage2D...). Those symbols don't
// exist on a host machine with no GPU/NDK -- which is exactly this sandbox.
//
// This file implements the exact same Texture methods (same header,
// render/Texture.h, completely unchanged) using a plain incrementing
// counter instead of real GL calls, and is compiled ONLY into the
// host_tests binary (see host_tests/build_and_run.sh) -- app/src/main/cpp's
// CMakeLists.txt never references this file, so the real Android build is
// completely unaffected by its existence.
//
// This is what lets host tests construct and Process() real ColorNode /
// BlurNode / TransformNode instances (see effects/nodes/) through a real
// RenderGraph, not just the GPU-free logic (Property, Keyframe, JSON,
// EffectPackageLoader) -- the actual node-graph execution path gets
// verified too, just against a fake "GPU" that hands out incrementing IDs
// instead of real texture names.

#include "render/Texture.h"

#include <atomic>
#include <utility>

namespace nle {

namespace {
std::atomic<unsigned int> gNextFakeTextureId{1};
}

Texture::~Texture() { Release(); }

Texture::Texture(Texture&& other) noexcept
    : id_(other.id_), target_(other.target_), width_(other.width_), height_(other.height_) {
    other.id_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        Release();
        id_ = other.id_;
        target_ = other.target_;
        width_ = other.width_;
        height_ = other.height_;
        other.id_ = 0;
    }
    return *this;
}

void Texture::Release() {
    // Real Texture::Release calls glDeleteTextures here. Nothing to free
    // for a fake id -- this stub's only job is to make the id 0 afterward,
    // matching the real class's "0 means invalid/released" invariant that
    // IsValid() and the move operations above depend on.
    id_ = 0;
}

Texture Texture::CreateEmpty(int width, int height, unsigned int internalFormat) {
    unsigned int id = gNextFakeTextureId.fetch_add(1);
    constexpr unsigned int kFakeTextureTarget = 0x0DE1;  // stand-in for GL_TEXTURE_2D; never dereferenced by a real GL call in host tests
    (void)internalFormat;
    return Texture(id, kFakeTextureTarget, width, height);
}

}  // namespace nle
