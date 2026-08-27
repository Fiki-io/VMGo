#include "input_dispatcher.h"
#include <linux/input.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>
#include <sys/time.h>

namespace vmgo {

InputDispatcher& InputDispatcher::getInstance() {
    static InputDispatcher instance;
    return instance;
}

bool InputDispatcher::initialize(const std::string& inputSocketPath) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    socketPath_ = inputSocketPath;

    // Connect to guest input socket
    inputFd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (inputFd_ < 0) {
        LOGE("Failed to create input socket: %s", strerror(errno));
        return false;
    }

    initialized_ = true;
    LOGI("Input Dispatcher initialized with target socket: %s", socketPath_.c_str());
    return true;
}

void InputDispatcher::shutdown() {
    std::lock_guard<std::mutex> lock(inputMutex_);
    if (inputFd_ >= 0) {
        close(inputFd_);
        inputFd_ = -1;
    }
    initialized_ = false;
    LOGI("Input Dispatcher shutdown");
}

void InputDispatcher::sendTouchEvent(TouchAction action, const std::vector<TouchPointer>& pointers) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    if (!initialized_ || pointers.empty()) return;

    // Multi-touch protocol Type B (Linux evdev)
    for (size_t i = 0; i < pointers.size(); ++i) {
        const auto& p = pointers[i];
        writeInputEvent(EV_ABS, ABS_MT_SLOT, static_cast<int32_t>(i));

        if (action == TouchAction::UP || action == TouchAction::CANCEL) {
            writeInputEvent(EV_ABS, ABS_MT_TRACKING_ID, -1);
        } else {
            writeInputEvent(EV_ABS, ABS_MT_TRACKING_ID, p.id);
            writeInputEvent(EV_ABS, ABS_MT_POSITION_X, static_cast<int32_t>(p.x));
            writeInputEvent(EV_ABS, ABS_MT_POSITION_Y, static_cast<int32_t>(p.y));
            writeInputEvent(EV_ABS, ABS_MT_TOUCH_MAJOR, static_cast<int32_t>(p.size * 30.0f));
            writeInputEvent(EV_ABS, ABS_MT_PRESSURE, static_cast<int32_t>(p.pressure * 255.0f));
        }
    }

    if (action == TouchAction::DOWN || action == TouchAction::POINTER_DOWN) {
        writeInputEvent(EV_KEY, BTN_TOUCH, 1);
    } else if (action == TouchAction::UP || action == TouchAction::CANCEL) {
        writeInputEvent(EV_KEY, BTN_TOUCH, 0);
    }

    syncEvent();
}

void InputDispatcher::sendKeyEvent(int keyCode, bool isDown) {
    std::lock_guard<std::mutex> lock(inputMutex_);
    if (!initialized_) return;

    // Translate Android KeyCode to Linux Evdev KeyCode
    uint16_t linuxKey = KEY_RESERVED;
    switch (keyCode) {
        case 3:   linuxKey = KEY_HOMEPAGE; break; // KEYCODE_HOME
        case 4:   linuxKey = KEY_BACK; break;     // KEYCODE_BACK
        case 24:  linuxKey = KEY_VOLUMEUP; break; // KEYCODE_VOLUME_UP
        case 25:  linuxKey = KEY_VOLUMEDOWN; break; // KEYCODE_VOLUME_DOWN
        case 26:  linuxKey = KEY_POWER; break;    // KEYCODE_POWER
        case 187: linuxKey = KEY_APPSELECT; break;// KEYCODE_APP_SWITCH (Recents)
        default:  linuxKey = static_cast<uint16_t>(keyCode); break;
    }

    writeInputEvent(EV_KEY, linuxKey, isDown ? 1 : 0);
    syncEvent();
}

void InputDispatcher::writeInputEvent(uint16_t type, uint16_t code, int32_t value) {
    if (inputFd_ < 0) return;

    struct input_event ev{};
    struct timeval tv{};
    gettimeofday(&tv, nullptr);

    ev.time.tv_sec = tv.tv_sec;
    ev.time.tv_usec = tv.tv_usec;
    ev.type = type;
    ev.code = code;
    ev.value = value;

    struct sockaddr_un addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    sendto(inputFd_, &ev, sizeof(ev), MSG_NOSIGNAL, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
}

void InputDispatcher::syncEvent() {
    writeInputEvent(EV_SYN, SYN_REPORT, 0);
}

} // namespace vmgo
