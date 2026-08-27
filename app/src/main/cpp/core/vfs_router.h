#ifndef VFS_ROUTER_H
#define VFS_ROUTER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "../include/vm_types.h"

namespace vmgo {

class VfsRouter {
public:
    static VfsRouter& getInstance();

    void initialize(const VmConfiguration& config);
    std::string resolvePath(const std::string& guestPath, int flags = 0);
    bool isVirtualDev(const std::string& guestPath) const;
    std::string getVirtualDevSocket(const std::string& guestPath) const;
    void addMountRule(const std::string& guestPrefix, const std::string& hostTarget);
    void reset();

    bool isInitialized() const { return initialized_; }
    const VmConfiguration& getConfig() const { return config_; }

private:
    VfsRouter() = default;
    ~VfsRouter() = default;

    std::string cleanPath(const std::string& path) const;

    VmConfiguration config_{};
    std::vector<std::pair<std::string, std::string>> mountTable_;
    std::unordered_map<std::string, std::string> virtualDevTable_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

} // namespace vmgo

#endif // VFS_ROUTER_H
