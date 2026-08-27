#include "egl_renderer.h"

#ifdef __ANDROID__
#include <android/native_window_jni.h>
#endif

namespace vmgo {

#ifdef __ANDROID__
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

static const GLfloat VERTICES[] = {
    -1.0f,  1.0f, 0.0f,   0.0f, 0.0f,
    -1.0f, -1.0f, 0.0f,   0.0f, 1.0f,
     1.0f,  1.0f, 0.0f,   1.0f, 0.0f,
     1.0f, -1.0f, 0.0f,   1.0f, 1.0f
};
#endif

EglRenderer& EglRenderer::getInstance() {
    static EglRenderer instance;
    return instance;
}

bool EglRenderer::initialize(ANativeWindow* window, int width, int height) {
    std::lock_guard<std::mutex> lock(renderMutex_);
    if (!window) return false;

    // If already initialized with the same window, just resize viewport
    if (initialized_ && nativeWindow_ == window) {
        displayWidth_ = width;
        displayHeight_ = height;
#ifdef __ANDROID__
        glViewport(0, 0, width, height);
#endif
        return true;
    }

    if (initialized_) {
        destroy();
    }

    nativeWindow_ = window;
    displayWidth_ = width;
    displayHeight_ = height;

#ifdef __ANDROID__
    ANativeWindow_setBuffersGeometry(window, width, height, WINDOW_FORMAT_RGBA_8888);
#endif

    if (!initEGL() || !initGL()) {
        destroy();
        return false;
    }

#ifdef __ANDROID__
    glViewport(0, 0, width, height);
#endif
    initialized_ = true;
    LOGI("EGL Renderer initialized successfully: %dx%d", width, height);
    return true;
}

bool EglRenderer::initEGL() {
#ifdef __ANDROID__
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
#endif
    return true;
}

bool EglRenderer::initGL() {
#ifdef __ANDROID__
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

    // Initial clear with dark background
    glClearColor(0.04f, 0.06f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(eglDisplay_, eglSurface_);
#endif
    return true;
}

GLuint EglRenderer::compileShader(GLenum type, const char* source) {
#ifdef __ANDROID__
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            std::vector<char> infoLog(infoLen);
            glGetShaderInfoLog(shader, infoLen, nullptr, infoLog.data());
            LOGE("Shader compilation error: %s", infoLog.data());
        }
        glDeleteShader(shader);
        return 0;
    }

    return shader;
#else
    (void)type;
    (void)source;
    return 0;
#endif
}

void EglRenderer::renderFrame(const uint8_t* pixels, int width, int height, int format) {
    std::lock_guard<std::mutex> lock(renderMutex_);
#ifdef __ANDROID__
    if (!initialized_ || !pixels || eglDisplay_ == EGL_NO_DISPLAY || eglSurface_ == EGL_NO_SURFACE) {
        return;
    }

    eglMakeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_);

    glUseProgram(programId_);

    // Upload pixel buffer
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureId_);

    GLenum glFormat = (format == 4) ? GL_RGB : GL_RGBA;
    GLenum glType = (format == 4) ? GL_UNSIGNED_SHORT_5_6_5 : GL_UNSIGNED_BYTE;

    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        glFormat,
        width,
        height,
        0,
        glFormat,
        glType,
        pixels
    );

    glUniform1i(samplerLoc_, 0);

    // Pass quad coordinates
    glVertexAttribPointer(positionLoc_, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), VERTICES);
    glEnableVertexAttribArray(positionLoc_);

    glVertexAttribPointer(texCoordLoc_, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &VERTICES[3]);
    glEnableVertexAttribArray(texCoordLoc_);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(positionLoc_);
    glDisableVertexAttribArray(texCoordLoc_);

    eglSwapBuffers(eglDisplay_, eglSurface_);
#else
    (void)pixels; (void)width; (void)height; (void)format;
#endif
}

void EglRenderer::updateSurface(ANativeWindow* window, int width, int height) {
    std::lock_guard<std::mutex> lock(renderMutex_);
    if (!window) return;

    if (nativeWindow_ == window && displayWidth_ == width && displayHeight_ == height) {
        return;
    }

    if (initialized_) {
        displayWidth_ = width;
        displayHeight_ = height;
#ifdef __ANDROID__
        glViewport(0, 0, displayWidth_, displayHeight_);
#endif
    }
}

void EglRenderer::destroy() {
    if (!initialized_) return;

#ifdef __ANDROID__
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
#endif

    nativeWindow_ = nullptr;
    initialized_ = false;
    LOGI("EGL Renderer destroyed");
}

} // namespace vmgo
