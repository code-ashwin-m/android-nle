// FakeGraphicsDevice.h
//
// A GraphicsDevice (render/GraphicsDevice.h) that never touches a GPU:
// CompileShader hands out an incrementing handle without compiling
// anything, DrawFullscreenQuad just records that it was called (with which
// shader and uniforms) instead of drawing, BindRenderTarget is a no-op.
// This is possible with zero changes to GraphicsDevice.h because it was
// already an abstract interface -- OpenGLRenderer.h/.cpp is the real GLES
// implementation nodes use on-device, and this is a second implementation
// used only here.
//
// What this validates: that ColorNode/BlurNode/TransformNode/CompositeNode
// etc. call the GraphicsDevice interface correctly (right shader compiled
// in OnAttach, right uniform names/values passed to DrawFullscreenQuad,
// right no-op skip when a parameter is at its neutral value) -- i.e. every
// node's *logic*, independent of whether GLES itself draws the pixels
// correctly (which requires a real device/emulator to verify, out of reach
// in this sandbox).

#pragma once

#include <string>
#include <vector>

#include "render/GraphicsDevice.h"

namespace nle {

class FakeGraphicsDevice : public GraphicsDevice {
public:
    struct DrawCall {
        ShaderHandle shader;
        std::vector<TextureBinding> textures;
        std::vector<std::pair<std::string, float>> floatUniforms;
    };

    ShaderHandle CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc) override {
        (void)vertexSrc;
        (void)fragmentSrc;
        return nextShaderHandle_++;
    }

    void DrawFullscreenQuad(ShaderHandle shader, const std::vector<TextureBinding>& textures,
                             const std::vector<std::pair<std::string, float>>& floatUniforms) override {
        drawCalls.push_back(DrawCall{shader, textures, floatUniforms});
    }

    void BindRenderTarget(Texture* target) override { lastBoundTarget = target; }

    // Convenience for tests: the float value passed for `name` in the most
    // recent draw call, or `fallback` if that draw call had no such
    // uniform (or there was no draw call at all).
    float LastUniform(const std::string& name, float fallback = 0.0f) const {
        if (drawCalls.empty()) return fallback;
        for (auto& [uniformName, value] : drawCalls.back().floatUniforms) {
            if (uniformName == name) return value;
        }
        return fallback;
    }

    std::vector<DrawCall> drawCalls;
    Texture* lastBoundTarget = nullptr;

private:
    ShaderHandle nextShaderHandle_ = 1;
};

}  // namespace nle
