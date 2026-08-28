#include <jni.h>
#include <string>
#include <vector>
#include <android/native_window_jni.h>

#include "../include/vm_types.h"
#include "../core/vfs_router.h"
#include "../core/seccomp_trap.h"
#include "../hal/qemu_pipe_server.h"
#include "../graphics/egl_renderer.h"
#include "../input/input_dispatcher.h"
#include "../storage/sparse_parser.h"
#include "../storage/ext4_extractor.h"
#include "../core/guest_bootstrapper.h"

#define JNI_CLASS_PATH "com/vmgo/app/core/NativeVmEngine"

using namespace vmgo;

static std::string jstringToString(JNIEnv* env, jstring jstr) {
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string str(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return str;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeSetLogFilePath(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jLogPath
) {
    std::string path = jstringToString(env, jLogPath);
    NativeLogger::getInstance().setLogFilePath(path);
    LOGI("Native logger initialized with path: %s", path.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeInit(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jSlotId,
    jstring jRootFsPath,
    jstring jSystemPath,
    jstring jVendorPath,
    jstring jDataPath,
    jstring jApexPath,
    jstring jSocketDir,
    jstring jNativeLibDir,
    jint width,
    jint height,
    jint dpi,
    jint fps,
    jboolean enableRoot,
    jboolean enableGapps
) {
    VmConfiguration config{};
    config.slotId = jstringToString(env, jSlotId);
    config.rootFsPath = jstringToString(env, jRootFsPath);
    config.systemPath = jstringToString(env, jSystemPath);
    config.vendorPath = jstringToString(env, jVendorPath);
    config.dataPath = jstringToString(env, jDataPath);
    config.apexPath = jstringToString(env, jApexPath);
    config.socketDir = jstringToString(env, jSocketDir);
    config.nativeLibDir = jstringToString(env, jNativeLibDir);

    config.displayWidth = width;
    config.displayHeight = height;
    config.displayDpi = dpi;
    config.targetFps = fps;
    config.enableRoot = enableRoot;
    config.enableGapps = enableGapps;

    // 1. Initialize VFS Path Router
    VfsRouter::getInstance().initialize(config);

    // 2. Initialize Input Dispatcher
    std::string inputSock = config.socketDir + "/input_event0.sock";
    InputDispatcher::getInstance().initialize(inputSock);

    // 4. Start QEMU Pipe HAL Server
    std::string qemuSock = config.socketDir + "/qemu_pipe.sock";
    QemuPipeServer::getInstance().start(qemuSock);

    LOGI("VM Go Native Engine initialized successfully for slot %s", config.slotId.c_str());
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeSetSurface(
    JNIEnv* env,
    jobject /* thiz */,
    jobject surface,
    jint width,
    jint height
) {
    if (!surface) {
        EglRenderer::getInstance().destroy();
        return JNI_TRUE;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        LOGE("Failed to get ANativeWindow from Surface");
        return JNI_FALSE;
    }

    bool success = EglRenderer::getInstance().initialize(window, width, height);
    return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeDestroySurface(
    JNIEnv* /* env */,
    jobject /* thiz */
) {
    EglRenderer::getInstance().destroy();
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeRenderFrame(
    JNIEnv* env,
    jobject /* thiz */,
    jbyteArray pixelArray,
    jint width,
    jint height,
    jint format
) {
    if (!pixelArray) return;
    jbyte* bytes = env->GetByteArrayElements(pixelArray, nullptr);
    if (bytes) {
        EglRenderer::getInstance().renderFrame(reinterpret_cast<const uint8_t*>(bytes), width, height, format);
        env->ReleaseByteArrayElements(pixelArray, bytes, JNI_ABORT);
    }
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeSendTouchEvent(
    JNIEnv* env,
    jobject /* thiz */,
    jint action,
    jintArray pIds,
    jfloatArray pXs,
    jfloatArray pYs,
    jfloatArray pPressures,
    jfloatArray pSizes,
    jint pointerCount
) {
    if (pointerCount <= 0) return;

    jint* ids = env->GetIntArrayElements(pIds, nullptr);
    jfloat* xs = env->GetFloatArrayElements(pXs, nullptr);
    jfloat* ys = env->GetFloatArrayElements(pYs, nullptr);
    jfloat* pressures = env->GetFloatArrayElements(pPressures, nullptr);
    jfloat* sizes = env->GetFloatArrayElements(pSizes, nullptr);

    std::vector<TouchPointer> pointers;
    pointers.reserve(pointerCount);

    for (int i = 0; i < pointerCount; ++i) {
        TouchPointer p{};
        p.id = ids ? ids[i] : i;
        p.x = xs ? xs[i] : 0.0f;
        p.y = ys ? ys[i] : 0.0f;
        p.pressure = pressures ? pressures[i] : 1.0f;
        p.size = sizes ? sizes[i] : 1.0f;
        pointers.push_back(p);
    }

    InputDispatcher::getInstance().sendTouchEvent(static_cast<TouchAction>(action), pointers);

    if (ids) env->ReleaseIntArrayElements(pIds, ids, JNI_ABORT);
    if (xs) env->ReleaseFloatArrayElements(pXs, xs, JNI_ABORT);
    if (ys) env->ReleaseFloatArrayElements(pYs, ys, JNI_ABORT);
    if (pressures) env->ReleaseFloatArrayElements(pPressures, pressures, JNI_ABORT);
    if (sizes) env->ReleaseFloatArrayElements(pSizes, sizes, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeSendKeyEvent(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jint keyCode,
    jboolean isDown
) {
    InputDispatcher::getInstance().sendKeyEvent(keyCode, isDown);
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeSendSensorUpdate(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jSensorName,
    jfloat v0,
    jfloat v1,
    jfloat v2
) {
    std::string name = jstringToString(env, jSensorName);
    QemuPipeServer::getInstance().sendSensorUpdate(name, v0, v1, v2);
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeSendGpsLocation(
    JNIEnv* /* env */,
    jobject /* thiz */,
    jdouble lat,
    jdouble lon,
    jfloat alt,
    jfloat speed,
    jfloat bearing,
    jlong timestamp
) {
    QemuPipeServer::getInstance().sendGpsLocation(lat, lon, alt, speed, bearing, static_cast<uint64_t>(timestamp));
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeUnsparseImage(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jSrcSparse,
    jstring jDstRaw
) {
    std::string src = jstringToString(env, jSrcSparse);
    std::string dst = jstringToString(env, jDstRaw);
    return SparseParser::unsparse(src, dst) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeIsSparseImage(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jFilePath
) {
    std::string path = jstringToString(env, jFilePath);
    return SparseParser::isSparseImage(path) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeGetEngineVersion(
    JNIEnv* env,
    jobject /* thiz */
) {
    std::string version = "VM Go Core Engine v1.0.0 (Native ARM64/ARM32 Seccomp+QEMUD)";
    return env->NewStringUTF(version.c_str());
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeExtractExt4Image(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jImagePath,
    jstring jTargetDir
) {
    std::string img = jstringToString(env, jImagePath);
    std::string target = jstringToString(env, jTargetDir);
    return Ext4Extractor::extractExt4Image(img, target) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeIsExt4Image(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jImagePath
) {
    std::string img = jstringToString(env, jImagePath);
    return Ext4Extractor::isExt4Image(img) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeLaunchGuest(
    JNIEnv* env,
    jobject /* thiz */,
    jstring jSlotId
) {
    std::string slotId = jstringToString(env, jSlotId);
    auto& vfs = VfsRouter::getInstance();
    if (!vfs.isInitialized()) {
        LOGE("Cannot launch guest: VFS Router not initialized");
        return JNI_FALSE;
    }
    const VmConfiguration& config = vfs.getConfig();
    bool ok = GuestBootstrapper::getInstance().launch(config);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeIsGuestAlive(
    JNIEnv* /* env */,
    jobject /* thiz */
) {
    return GuestBootstrapper::getInstance().isAlive() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeKillGuest(
    JNIEnv* /* env */,
    jobject /* thiz */
) {
    GuestBootstrapper::getInstance().kill();
}

JNIEXPORT void JNICALL
Java_com_vmgo_app_core_NativeVmEngine_nativeStopVm(
    JNIEnv* /* env */,
    jobject /* thiz */
) {
    GuestBootstrapper::getInstance().kill();
    QemuPipeServer::getInstance().stop();
    InputDispatcher::getInstance().shutdown();
    SeccompTrap::getInstance().uninstall();
    EglRenderer::getInstance().destroy();
    VfsRouter::getInstance().reset();
    LOGI("VM Go Native Engine stopped");
}

} // extern "C"
