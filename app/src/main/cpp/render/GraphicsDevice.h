// GraphicsDevice.h
//
// The spec requires "Use OpenGL ES initially. Design renderer so Vulkan can
// replace OpenGL later. Graphics API abstraction is preferred." This is
// that abstraction. Every RenderNode that needs to draw (ColorConvert,
// Brightness, Composite, Output) goes through GraphicsDevice, never
// through GLES headers directly. When a Vulkan backend is added, it
// implements this same interface (VulkanRenderer) and RenderNode
// subclasses do not change at all -- only whichever factory constructs the
// GraphicsDevice for a given RenderContext changes.
//
// The interface is intentionally small and shader-source-based rather than
// exposing GL-specific concepts like "bind this texture unit" -- shader
// programs are identified by an opaque ShaderHandle and the device manages
// binding internally, which is the kind of GL-specific detail that would
// leak the abstraction if exposed here.

#pragma once

#include <string>

#include "render/Texture.h"

namespace nle {

using ShaderHandle = int;
constexpr ShaderHandle kInvalidShader = -1;

// Describes one texture input to a draw call, with the uniform name the
// shader expects it bound to.
struct TextureBinding {
    unsigned int textureId = 0;
    unsigned int textureTarget = 0;
    std::string uniformName;
};

class GraphicsDevice {
public:
    virtual ~GraphicsDevice() = default;

    // Compiles a vertex+fragment shader pair, returns a reusable handle.
    // Nodes call this once in OnAttach(), not per-frame.
    virtual ShaderHandle CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc) = 0;

    // Renders a full-screen textured quad using the given shader and
    // texture bindings, into the currently bound framebuffer/texture.
    // Every node that does simple per-pixel work (Brightness, LUT, color
    // convert) is expressible as one DrawFullscreenQuad call with a
    // different fragment shader -- this single entry point is why adding
    // a new filter-style effect node is typically ~30 lines of shader code
    // and no new draw logic.
    virtual void DrawFullscreenQuad(ShaderHandle shader, const std::vector<TextureBinding>& textures,
                                     const std::vector<std::pair<std::string, float>>& floatUniforms) = 0;

    // Binds `target` as the render target for subsequent draw calls.
    // Passing nullptr targets the default framebuffer (the on-screen
    // preview surface, or the encoder's input surface).
    virtual void BindRenderTarget(Texture* target) = 0;
};

}  // namespace nle
