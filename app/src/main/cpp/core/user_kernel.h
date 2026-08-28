#ifndef USER_KERNEL_H
#define USER_KERNEL_H

#include "../include/vm_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace vmgo {

class UserKernel {
public:
    static UserKernel& getInstance();

    void initialize(const VmConfiguration& config);
    void reset();

    // Syscall emulations
    int sysMount(const char* source, const char* target, const char* filesystemtype, unsigned long mountflags, const void* data);
    int sysUmount(const char* target, int flags);
    int sysChroot(const char* path);
    int sysPivotRoot(const char* newRoot, const char* putOld);
    int sysMknodat(int dirfd, const char* pathname, mode_t mode, dev_t dev);

    // Property service helpers
    void setProperty(const std::string& key, const std::string& value);
    std::string getProperty(const std::string& key, const std::string& defaultVal = "") const;

    // Device node setup
    void setupVirtualDevNodes(const std::string& rootfs);

private:
    UserKernel() = default;
    ~UserKernel() = default;

    mutable std::mutex mutex_;
    bool initialized_ = false;
    VmConfiguration config_{};
    std::unordered_map<std::string, std::string> properties_;
};

} // namespace vmgo

#endif // USER_KERNEL_H
