#ifndef INPUT_DISPATCHER_H
#define INPUT_DISPATCHER_H

#include "../include/vm_types.h"
#include <string>
#include <vector>
#include <mutex>

namespace vmgo {

class InputDispatcher {
public:
    static InputDispatcher& getInstance();

    bool initialize(const std::string& inputSocketPath);
    void shutdown();

    void sendTouchEvent(TouchAction action, const std::vector<TouchPointer>& pointers);
    void sendKeyEvent(int keyCode, bool isDown);

private:
    InputDispatcher() = default;
    ~InputDispatcher() { shutdown(); }

    void writeInputEvent(uint16_t type, uint16_t code, int32_t value);
    void syncEvent();

    std::string socketPath_;
    int inputFd_ = -1;
    std::mutex inputMutex_;
    bool initialized_ = false;
};

} // namespace vmgo

#endif // INPUT_DISPATCHER_H
