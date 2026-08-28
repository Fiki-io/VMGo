#ifndef GUEST_BOOTSTRAPPER_H
#define GUEST_BOOTSTRAPPER_H

#include "../include/vm_types.h"
#include <string>
#include <functional>
#include <sys/types.h>

namespace vmgo {

class GuestBootstrapper {
public:
    static GuestBootstrapper& getInstance();

    using LogCallback = std::function<void(const std::string& line)>;

    bool launch(const VmConfiguration& config, LogCallback onLog = nullptr);
    bool isAlive() const;
    void kill();
    pid_t getGuestPid() const { return guestPid_; }

private:
    GuestBootstrapper() = default;
    ~GuestBootstrapper() { kill(); }

    void setupEnvironment(const VmConfiguration& config);
    void createVirtualDevNodes(const std::string& rootfs);
    void setupTimezoneData(const std::string& rootfs);
    void startLogReader(int pipeFd);

    pid_t guestPid_ = -1;
    int logPipeFd_ = -1;
    bool running_ = false;
};

} // namespace vmgo

#endif // GUEST_BOOTSTRAPPER_H
