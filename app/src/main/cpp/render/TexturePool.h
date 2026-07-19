// TexturePool.h
//
// glTexImage2D allocation is expensive relative to a 16ms frame budget at
// 60fps. Every intermediate RenderNode (ColorConvert, Brightness,
// Composite) needs a scratch texture to render into every frame; without
// pooling, that's several GPU allocations per frame, per node, which is
// exactly the kind of per-frame allocation the spec's "Object pools,
// Texture pools, Frame pools, GPU resource reuse" requirement calls out.
//
// Textures are bucketed by (width, height, internalFormat) since a texture
// sized for a 1080x1920 canvas can't be reused at a different export
// resolution. Acquire() pulls a same-sized texture from the free list if
// one exists, or allocates a new one; Release() returns it to the pool
// rather than deleting it. The pool itself owns every Texture it has ever
// created for the lifetime of the render thread's GL context.

#pragma once

#include <map>
#include <memory>
#include <tuple>
#include <vector>

#include "render/Texture.h"

namespace nle {

class TexturePool {
public:
    // Returned texture is borrowed; caller must Release() it back once done
    // with it for this frame's execution. Never delete the pointer.
    Texture* Acquire(int width, int height, unsigned int internalFormat) {
        Key key{width, height, internalFormat};
        auto& freeList = free_[key];
        if (!freeList.empty()) {
            Texture* tex = freeList.back();
            freeList.pop_back();
            inUse_.push_back(tex);
            return tex;
        }
        allTextures_.push_back(std::make_unique<Texture>(Texture::CreateEmpty(width, height, internalFormat)));
        Texture* tex = allTextures_.back().get();
        inUse_.push_back(tex);
        return tex;
    }

    void Release(Texture* texture) {
        auto it = std::find(inUse_.begin(), inUse_.end(), texture);
        if (it == inUse_.end()) return;  // not currently borrowed; ignore
        inUse_.erase(it);
        Key key{texture->Width(), texture->Height(), 0 /* internalFormat not tracked on Texture; see note below */};
        // NOTE: for Phase 1 all pooled textures share one internal format
        // (RGBA8), so bucketing on (width, height) alone is sufficient.
        // If/when HDR formats are added, Texture should carry its
        // internalFormat so it round-trips through this key correctly.
        free_[{texture->Width(), texture->Height(), 0}].push_back(texture);
    }

    size_t TotalAllocated() const { return allTextures_.size(); }
    size_t InUseCount() const { return inUse_.size(); }

private:
    using Key = std::tuple<int, int, unsigned int>;

    std::vector<std::unique_ptr<Texture>> allTextures_;
    std::vector<Texture*> inUse_;
    std::map<Key, std::vector<Texture*>> free_;
};

}  // namespace nle
