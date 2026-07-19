#include "render/OpenGLRenderer.h"

#include <GLES3/gl3.h>
#include <android/log.h>

#include <vector>

#define LOG_TAG "OpenGLRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace nle {

namespace {
// A single triangle-strip full-screen quad in clip space, with matching UVs.
// Y is flipped in UV space (1 - v) because decoded/composited frames are
// stored top-down while GL texture origin is bottom-left.
constexpr float kQuadVertices[] = {
    // x,    y,    u,    v
    -1.0f, -1.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 0.0f,
};
}  // namespace

OpenGLRenderer::OpenGLRenderer() = default;

OpenGLRenderer::~OpenGLRenderer() {
    if (quadVbo_) glDeleteBuffers(1, &quadVbo_);
    if (framebuffer_) glDeleteFramebuffers(1, &framebuffer_);
    for (auto& shader : shaders_) {
        if (shader.programId) glDeleteProgram(shader.programId);
    }
}

void OpenGLRenderer::EnsureQuadGeometry() {
    if (quadVbo_) return;
    glGenBuffers(1, &quadVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

unsigned int OpenGLRenderer::CompileStage(unsigned int stageType, const std::string& src) {
    unsigned int shader = glCreateShader(stageType);
    const char* srcPtr = src.c_str();
    glShaderSource(shader, 1, &srcPtr, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen > 0 ? logLen : 1);
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        LOGE("Shader compile failed: %s", log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

ShaderHandle OpenGLRenderer::CompileShader(const std::string& vertexSrc, const std::string& fragmentSrc) {
    unsigned int vs = CompileStage(GL_VERTEX_SHADER, vertexSrc);
    unsigned int fs = CompileStage(GL_FRAGMENT_SHADER, fragmentSrc);
    if (!vs || !fs) return kInvalidShader;

    unsigned int program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!linked) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        std::vector<char> log(logLen > 0 ? logLen : 1);
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        LOGE("Program link failed: %s", log.data());
        glDeleteProgram(program);
        return kInvalidShader;
    }

    shaders_.push_back({program});
    return static_cast<ShaderHandle>(shaders_.size() - 1);
}

void OpenGLRenderer::BindRenderTarget(Texture* target) {
    if (!target) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);  // default framebuffer: on-screen or encoder surface
        return;
    }
    if (!framebuffer_) glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target->Id(), 0);
    glViewport(0, 0, target->Width(), target->Height());

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("Framebuffer incomplete when targeting texture %u", target->Id());
    }
}

void OpenGLRenderer::DrawFullscreenQuad(ShaderHandle shader, const std::vector<TextureBinding>& textures,
                                         const std::vector<std::pair<std::string, float>>& floatUniforms) {
    if (shader < 0 || shader >= static_cast<ShaderHandle>(shaders_.size())) return;
    EnsureQuadGeometry();

    unsigned int program = shaders_[shader].programId;
    glUseProgram(program);

    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    GLint posLoc = glGetAttribLocation(program, "aPosition");
    GLint uvLoc = glGetAttribLocation(program, "aTexCoord");
    constexpr GLsizei stride = 4 * sizeof(float);
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(uvLoc);
    glVertexAttribPointer(uvLoc, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));

    for (size_t i = 0; i < textures.size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
        glBindTexture(textures[i].textureTarget, textures[i].textureId);
        GLint loc = glGetUniformLocation(program, textures[i].uniformName.c_str());
        if (loc >= 0) glUniform1i(loc, static_cast<GLint>(i));
    }

    for (auto& [name, value] : floatUniforms) {
        GLint loc = glGetUniformLocation(program, name.c_str());
        if (loc >= 0) glUniform1f(loc, value);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(posLoc);
    glDisableVertexAttribArray(uvLoc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

}  // namespace nle
