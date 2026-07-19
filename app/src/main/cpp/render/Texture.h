// Texture.h
//
// RAII around a GL texture name so a texture is always deleted exactly
// once, even on early-return / exception paths -- the C++ standard
// answer to the spec's "avoid unnecessary frame copies" via correct
// resource lifetime rather than manual glDeleteTextures bookkeeping.
// Move-only: copying a GL texture handle would let two Texture objects
// both believe they own (and both try to delete) the same GL name.

#pragma once

namespace nle {

class Texture {
public:
    Texture() = default;
    Texture(unsigned int id, unsigned int target, int width, int height)
        : id_(id), target_(target), width_(width), height_(height) {}

    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    unsigned int Id() const { return id_; }
    unsigned int Target() const { return target_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsValid() const { return id_ != 0; }

    static Texture CreateEmpty(int width, int height, unsigned int internalFormat);

private:
    void Release();

    unsigned int id_ = 0;
    unsigned int target_ = 0;  // GL_TEXTURE_2D, GL_TEXTURE_EXTERNAL_OES, etc.
    int width_ = 0;
    int height_ = 0;
};

}  // namespace nle
