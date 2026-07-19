// RenderNode.h
//
// This interface is the entire reason the spec insists on a render *graph*
// instead of a fixed pipeline: every stage -- decode, color convert,
// brightness, composite, output -- is the same shape from the executor's
// point of view. Adding Blur, LUT, Crop, Transform, Mask, Text,
// Transitions, or Blend Modes later means writing one new class that
// implements this interface and wiring it into a graph description; it
// never means touching RenderGraph's execution logic or any existing node.
//
// Why Process() takes a vector of input Frames rather than exactly one:
// CompositeNode needs N inputs (one per active track), and future
// TransitionNode needs exactly 2. A single-input signature would force
// composite/transition nodes to use a different interface, which is the
// "fixed pipeline" trap the spec explicitly warns against.
//
// Why RenderContext exists: nodes need access to shared GPU resources
// (TexturePool, GLContext) and the current evaluation time, but should not
// each independently reach into the engine to get them -- RenderContext is
// assembled once per graph execution and passed down, so a node's
// dependencies are visible in one place (this file) rather than scattered.

#pragma once

#include <string>
#include <vector>

#include "nle/core/Types.h"
#include "rendergraph/Frame.h"

namespace nle {

class TexturePool;
class GLContext;
class GraphicsDevice;

struct RenderContext {
    TimeUs time = 0;              // playhead time being rendered, in timeline space
    TexturePool* texturePool = nullptr;
    GLContext* glContext = nullptr;
    // Draw/shader operations go through this abstraction, not raw GL calls,
    // so nodes stay portable to the future Vulkan backend (see
    // render/OpenGLRenderer.h for the rationale).
    GraphicsDevice* graphicsDevice = nullptr;
    int outputWidth = 0;
    int outputHeight = 0;
};

class RenderNode {
public:
    virtual ~RenderNode() = default;

    // Produces one output Frame from zero or more input Frames. Nodes must
    // be side-effect-free with respect to *other* nodes -- the only shared
    // mutable state they may touch is what RenderContext explicitly hands
    // them (e.g. borrowing a texture from the pool), which keeps graph
    // execution order-independent wherever the graph shape allows
    // reordering (useful later for parallel branch execution).
    virtual Frame Process(const std::vector<Frame>& inputs, const RenderContext& context) = 0;

    virtual std::string Name() const = 0;

    // Called once when the node enters the graph (e.g. to allocate a
    // persistent shader program) and once when it's removed. Separating
    // this from the constructor/destructor lets nodes be constructed
    // off-thread and initialized lazily on the render thread, where the
    // GL context actually lives.
    virtual void OnAttach(const RenderContext& /*context*/) {}
    virtual void OnDetach() {}
};

}  // namespace nle
