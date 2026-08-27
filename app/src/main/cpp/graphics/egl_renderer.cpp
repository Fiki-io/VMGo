#include "egl_renderer.h"
#include <android/native_window_jni.h>

namespace vmgo {

static const char* VERTEX_SHADER = R"(
    attribute vec4 a_Position;
    attribute vec2 a_TexCoord;
    varying vec2 v_TexCoord;
    void main() {
        gl_Position = a_Position;
        v_TexCoord = a_TexCoord;
    }
)";

static const char* FRAGMENT_SHADER = R"(
    precision mediump float;
    varying vec2 v_TexCoord;
    uniform sampler2D u_Texture;
    void main() {
        gl_FragColor = texture2D(u_Texture, v_TexCoord);
    }
)";

// Full-screen quad (flipped vertically to match Android framebuffer coordinate system)
static const GLfloat VERTICES[] = {
    -1.0f,  1.0f, 0.0f,   0.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,   0.0f, 1.0f,
     1.0f,  1.0f, 0.0f,   1.0f, 0.0f,
     1.0f, -1.0f, 0.0f,   1.0f, 1.0f
};

EglRenderer& EglRenderer::getInstance() {
    static EglRenderer instance;
    return instance;
}

bool EglRenderer::initialize(ANativeWindow* window, int width, int height) {
    std::lock_guard<std::mutex> lock(renderMutex_);
    if (initialized_) {
        destroy();
    }

    nativeWindow_ = window;
    displayWidth_ = width;
    displayHeight_ = height;

    if (!initEGL() || !initGL()) {
        destroy();
        return false;
    }

    initialized_ = true;
    LOGI("EGL Renderer initialized successfully: %dx%d", width, height);
    return true;
}

bool EglRenderer::initEGL() {
    eglDisplay_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (eglDisplay_ == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(eglDisplay_, &major, &minor)) {
        LOGE("eglInitialize failed");
        return false;
    }

    const EGLint configAttribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };

    EGLint numConfigs = 0;
    if (!eglChooseConfig(eglDisplay_, configAttribs, &eglConfig_, 1, &numConfigs) || numConfigs <= 0) {
        LOGE("eglChooseConfig failed");
        return false;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, contextAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed");
        return false;
    }

    eglSurface_ = eglCreateWindowSurface(eglDisplay_, eglConfig_, nativeWindow_, nullptr);
    if (eglSurface_ == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed");
        return false;
    }

    if (!eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
        LOGE("eglMakeCurrent failed");
        return false;
    }

    return true;
}

bool EglRenderer::initGL() {
    GLuint vShader = compileShader(GL_VERTEX_SHADER, VERTEX_SHADER);
    GLuint fShader = compileShader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);

    if (!vShader || !fShader) return false;

    programId_ = glCreateProgram();
    glAttachShader(programId_, vShader);
    glAttachShader(programId_, fShader);
    glLinkProgram(programId_);

    GLint linked = 0;
    glGetProgramiv(programId_, GL_LINK_STATUS, &linked);
    if (!linked) {
        LOGE("GL Program link failed");
        return false;
    }

    positionLoc_ = glGetAttribLocation(programId_, "a_Position");
    texCoordLoc_ = glGetAttribLocation(programId_, "a_TexCoord");
    samplerLoc_ = glGetUniformLocation(programId_, "u_Texture");

    // Generate texture
    glGenTextures(1, &textureId_);
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glViewport(0, 0, displayWidth_, displayHeight_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

GLuint EglRenderer::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char info[512];
        glGetShaderInfoLog(shader, sizeof(info), nullptr, info);
        LOGE("Shader compilation failed: %s", info);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void EglRenderer::renderFrame(const uint8_t* pixelBuffer, int bufferWidth, int bufferHeight, int format) {
    std::lock_guard<std::mutex> lock(renderMutex_);
    if (!initialized_ || !pixelBuffer) return;

    eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(programId_);
    glBindTexture(GL_TEXTURE_2D, textureId_);

    GLenum glFormat = (format == 4) ? GL_RGB565 : GL_RGBA;
    GLenum glType = (format == 4) ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;

    if (textureWidth_ != bufferWidth || textureHeight_ != bufferHeight) {
        glTexImage2D(GL_TEXTURE_2D, 0, glFormat, bufferWidth, bufferHeight, 0, glFormat, glType, pixelBuffer);
        textureWidth_ = bufferWidth;
        textureHeight_ = bufferHeight;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bufferWidth, bufferHeight, glFormat, glType, pixelBuffer);
    }

    glUniform1i(samplerLoc_, 0);

    glVertexAttribPointer(positionLoc_, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), VERTICES);
    glEnableVertexAttribArray(positionLoc_);

    glVertexAttribPointer(texCoordLoc_, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &VERTICES[3]);
    glEnableVertexAttribArray(texCoordLoc_);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    eglSwapBuffers(eglDisplay_, eglSurface_);
}

void EglRenderer::updateSurface(ANativeWindow* window, int width, int height) {
    std::lock_guard<std::mutex> lock(renderMutex_);
    if (nativeWindow_ == window && displayWidth_ == width && displayHeight_ == height) {
        return;
    }

    if (initialized_) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglSurface_ != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay_, eglSurface_);
        }
        nativeWindow_ = window;
        displayWidth_ = width;
        displayHeight_ = height;

        eglSurface_ = eglCreateWindowSurface(eglDisplay_, eglConfig_, nativeWindow_, nullptr);
        eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_);
        glViewport(0, 0, displayWidth_, displayHeight_);
    }
}

void EglRenderer::destroy() {
    if (!initialized_) return;

    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglSurface_ != EGL_NO_SURFACE) {
            eglDestroySurface(eglDisplay_, eglSurface_);
            eglSurface_ = EGL_NO_SURFACE;
        }
        if (eglContext_ != EGL_NO_CONTEXT) {
            eglDestroyContext(eglDisplay_, eglContext_);
            eglContext_ = EGL_NO_CONTEXT;
        }
        eglTerminate(eglDisplay_);
        eglDisplay_ = EGL_NO_DISPLAY;
    }

    if (programId_) {
        glDeleteProgram(programId_);
        programId_ = 0;
    }
    if (textureId_) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }

    nativeWindow_ = nullptr;
    initialized_ = false;
    LOGI("EGL Renderer destroyed");
}

} // namespace vmgo
