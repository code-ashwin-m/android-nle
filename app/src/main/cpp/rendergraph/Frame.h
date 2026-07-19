// Frame.h
//
// Frame is what flows along the edges of the render graph: "Decoder Node ->
// Color Convert Node -> Brightness Node -> Composite Node -> Output". It
// wraps a GPU texture handle (never raw pixel bytes -- see the "avoid
// unnecessary frame copies" requirement) plus the metadata a node needs to
// process it correctly.
//
// Ownership: a Frame does NOT own its GL texture. Textures are owned by
// TexturePool (render/TexturePool.h) and merely borrowed for the duration
// of one graph execution, then returned. This is what makes "no unnecessary
// frame copies" possible -- a Frame can be passed by value between nodes
// cheaply because it's just a handle + metadata, not pixel data.

#pragma once

#include "nle/core/Types.h"

namespace nle {

struct Frame {
    unsigned int textureId = 0;     // GL texture name, 0 = invalid/no-op frame
    unsigned int textureTarget = 0; // e.g. GL_TEXTURE_2D or GL_TEXTURE_EXTERNAL_OES
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    TimeUs presentationTimeUs = kTimeInvalid;

    bool IsValid() const { return textureId != 0; }
};

}  // namespace nle
