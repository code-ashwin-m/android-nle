#include "render/Texture.h"

#include <GLES3/gl3.h>

#include <utility>

namespace nle {

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
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
}

Texture Texture::CreateEmpty(int width, int height, unsigned int internalFormat) {
    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return Texture(id, GL_TEXTURE_2D, width, height);
}

}  // namespace nle
