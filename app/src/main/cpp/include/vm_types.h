#ifndef VM_TYPES_H
#define VM_TYPES_H

#include <cstdint>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <mutex>

#ifdef __ANDROID__
#include <android/log.h>
#define NATIVE_LOG_TAG "VMGo-Native"
#else
#define NATIVE_LOG_TAG "VMGo-Native"
#endif

namespace vmgo {

class NativeLogger {
public:
    static NativeLogger& getInstance() {
        static NativeLogger instance;
        return instance;
    }

    void setLogFilePath(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        logFilePath_ = path;
    }

    void log(int priority, const char* tag, const char* fmt, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

#ifdef __ANDROID__
        int androidPrio = ANDROID_LOG_INFO;
        const char* levelStr = "INFO";
        if (priority == 3) { androidPrio = ANDROID_LOG_DEBUG; levelStr = "DEBUG"; }
        else if (priority == 4) { androidPrio = ANDROID_LOG_INFO; levelStr = "INFO"; }
        else if (priority == 5) { androidPrio = ANDROID_LOG_WARN; levelStr = "WARN"; }
        else if (priority >= 6) { androidPrio = ANDROID_LOG_ERROR; levelStr = "ERROR"; }

        __android_log_print(androidPrio, tag, "%s", buffer);
#else
        const char* levelStr = (priority >= 6) ? "ERROR" : ((priority == 5) ? "WARN" : "INFO");
        printf("[%s] [%s]: %s\n", levelStr, tag, buffer);
#endif

        // Write directly to persistent log file
        std::lock_guard<std::mutex> lock(mutex_);
        if (!logFilePath_.empty()) {
            FILE* fp = fopen(logFilePath_.c_str(), "a");
            if (fp) {
                time_t now = time(nullptr);
                struct tm* t = localtime(&now);
                char timeBuf[64];
                strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", t);

                fprintf(fp, "[%s] [%s] [%s]: %s\n", timeBuf, levelStr, tag, buffer);
                fflush(fp);
                fclose(fp);
            }
        }
    }

private:
    NativeLogger() = default;
    ~NativeLogger() = default;

    std::string logFilePath_;
    std::mutex mutex_;
};

} // namespace vmgo

#define LOGD(fmt, ...) vmgo::NativeLogger::getInstance().log(3, NATIVE_LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGI(fmt, ...) vmgo::NativeLogger::getInstance().log(4, NATIVE_LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) vmgo::NativeLogger::getInstance().log(5, NATIVE_LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) vmgo::NativeLogger::getInstance().log(6, NATIVE_LOG_TAG, fmt, ##__VA_ARGS__)

namespace vmgo {

enum class VmState {
    STOPPED = 0,
    INITIALIZING = 1,
    STARTING = 2,
    RUNNING = 3,
    PAUSED = 4,
    ERROR = 5
};

enum class HalChannelType {
    UNKNOWN = 0,
    SENSORS = 1,
    GPS = 2,
    CAMERA = 3,
    AUDIO = 4,
    WIFI = 5,
    GSM = 6,
    HW_CONTROL = 7,
    BOOT_PROPERTIES = 8,
    ADB = 9
};

enum class TouchAction {
    DOWN = 0,
    UP = 1,
    MOVE = 2,
    CANCEL = 3,
    POINTER_DOWN = 5,
    POINTER_UP = 6
};

struct TouchPointer {
    int id;
    float x;
    float y;
    float pressure;
    float size;
};

struct VmConfiguration {
    std::string slotId;
    std::string rootFsPath;
    std::string systemPath;
    std::string vendorPath;
    std::string dataPath;
    std::string apexPath;
    std::string socketDir;
    std::string nativeLibDir;

    int displayWidth;
    int displayHeight;
    int displayDpi;
    int targetFps;

    bool enableRoot;
    bool enableGapps;
    bool enableAudio;
    bool enableCamera;
    bool enableSensors;
    bool enableGps;

    std::string brand;
    std::string model;
    std::string device;
    std::string serial;
    std::string macAddress;
    std::string imei;
};

struct FrameBufferInfo {
    int width;
    int height;
    int stride;
    int format; // 1: RGBA_8888, 4: RGB_565
    uint8_t* pixels;
    size_t size;
    uint64_t frameNumber;
    uint64_t timestampNs;
};

} // namespace vmgo

#endif // VM_TYPES_H
