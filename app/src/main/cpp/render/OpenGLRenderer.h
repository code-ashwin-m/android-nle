// OpenGLRenderer.h
//
// Concrete GLES3 implementation of GraphicsDevice. Everything specific to
// "how GL actually draws a full-screen quad" -- VBO setup, framebuffer
// binding, shader linking -- is contained in this one .cpp so that a future
// VulkanRenderer can be added as a sibling file without touching any
// RenderNode. One instance of this class lives for the lifetime of a
// GLContext (see engine/EditorEngine.h for how preview vs export each get
// their own GLContext + OpenGLRenderer pair).

#pragma once

#include <unordered_map>
#include <vector>

#include "render/GraphicsDevice.h"

namespace nle {

class OpenGLRenderer : public GraphicsDevice {
public:
    OpenGLRenderer();
    ~OpenGLRenderer() override;

    ShaderHandle CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc) override;
    void DrawFullscreenQuad(ShaderHandle shader, const std::vector<TextureBinding>& textures,
                             const std::vector<std::pair<std::string, float>>& floatUniforms) override;
    void BindRenderTarget(Texture* target) override;

private:
    struct ShaderProgram {
        unsigned int programId = 0;
    };

    unsigned int quadVbo_ = 0;
    unsigned int framebuffer_ = 0;  // lazily created FBO used when targeting an offscreen Texture
    std::vector<ShaderProgram> shaders_;

    void EnsureQuadGeometry();
    static unsigned int CompileStage(unsigned int stageType, const std::string& src);
};

}  // namespace nle
