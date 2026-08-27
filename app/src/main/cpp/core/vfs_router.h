#ifndef VFS_ROUTER_H
#define VFS_ROUTER_H

#include "../include/vm_types.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

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

private:
    VfsRouter() = default;
    ~VfsRouter() = default;

    std::string cleanPath(const std::string& path);

    mutable std::mutex mutex_;
    VmConfiguration config_;
    bool initialized_ = false;

    // Mapping prefix -> host path
    std::vector<std::pair<std::string, std::string>> mountTable_;
    std::unordered_map<std::string, std::string> virtualDevTable_;
};

} // namespace vmgo

#endif // VFS_ROUTER_H
