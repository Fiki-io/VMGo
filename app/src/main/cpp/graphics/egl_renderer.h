#ifndef EGL_RENDERER_H
#define EGL_RENDERER_H

#include "../include/vm_types.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <mutex>

namespace vmgo {

class EglRenderer {
public:
    static EglRenderer& getInstance();

    bool initialize(ANativeWindow* window, int width, int height);
    void destroy();

    void updateSurface(ANativeWindow* window, int width, int height);
    void renderFrame(const uint8_t* pixelBuffer, int bufferWidth, int bufferHeight, int format);

    bool isReady() const { return initialized_; }

private:
    EglRenderer() = default;
    ~EglRenderer() { destroy(); }

    bool initEGL();
    bool initGL();
    GLuint compileShader(GLenum type, const char* source);

    std::mutex renderMutex_;
    bool initialized_ = false;

    ANativeWindow* nativeWindow_ = nullptr;
    int displayWidth_ = 0;
    int displayHeight_ = 0;

    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLConfig eglConfig_ = nullptr;

    GLuint programId_ = 0;
    GLuint textureId_ = 0;
    GLint positionLoc_ = -1;
    GLint texCoordLoc_ = -1;
    GLint samplerLoc_ = -1;

    int textureWidth_ = 0;
    int textureHeight_ = 0;
};

} // namespace vmgo

#endif // EGL_RENDERER_H
