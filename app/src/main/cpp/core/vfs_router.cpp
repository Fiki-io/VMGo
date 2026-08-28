#include "vfs_router.h"
#include <unistd.h>
#include <sys/stat.h>
#include <algorithm>

namespace vmgo {

VfsRouter& VfsRouter::getInstance() {
    static VfsRouter instance;
    return instance;
}

void VfsRouter::initialize(const VmConfiguration& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    mountTable_.clear();
    virtualDevTable_.clear();

    // Priority 1: APEX Modules (/apex -> $apexPath)
    if (!config.apexPath.empty()) {
        mountTable_.emplace_back("/apex", config.apexPath);
    }

    // Priority 2: Vendor Platform (/vendor -> $vendorPath)
    if (!config.vendorPath.empty()) {
        mountTable_.emplace_back("/vendor", config.vendorPath);
        mountTable_.emplace_back("/system/vendor", config.vendorPath);
    }

    // Priority 3: System Partition (/system -> $systemPath)
    if (!config.systemPath.empty()) {
        mountTable_.emplace_back("/system", config.systemPath);
    }

    // Priority 4: User Data (/data -> $dataPath)
    if (!config.dataPath.empty()) {
        mountTable_.emplace_back("/data", config.dataPath);
    }

    // Priority 5: Virtual Devices (/dev/qemu_pipe, /dev/ashmem, /dev/input)
    std::string sockBase = config.socketDir.empty() ? (config.rootFsPath + "/dev") : config.socketDir;
    virtualDevTable_["/dev/qemu_pipe"] = sockBase + "/qemu_pipe.sock";
    virtualDevTable_["/dev/goldfish_pipe"] = sockBase + "/qemu_pipe.sock";
    virtualDevTable_["/dev/input/event0"] = sockBase + "/input_event0.sock";
    virtualDevTable_["/dev/input/event1"] = sockBase + "/input_event1.sock";
    virtualDevTable_["/dev/ashmem"] = sockBase + "/ashmem.sock";

    initialized_ = true;
    LOGI("VFS Router initialized with rootFs: %s", config.rootFsPath.c_str());
}

bool VfsRouter::isVirtualDev(const std::string& guestPath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return virtualDevTable_.find(guestPath) != virtualDevTable_.end();
}

std::string VfsRouter::getVirtualDevSocket(const std::string& guestPath) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = virtualDevTable_.find(guestPath);
    if (it != virtualDevTable_.end()) {
        return it->second;
    }
    return "";
}

void VfsRouter::addMountRule(const std::string& guestPrefix, const std::string& hostTarget) {
    std::lock_guard<std::mutex> lock(mutex_);
    mountTable_.insert(mountTable_.begin(), {guestPrefix, hostTarget});
}

std::string VfsRouter::resolvePath(const std::string& guestPath, int /* flags */) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || guestPath.empty()) {
        return guestPath;
    }

    // Clean double slashes
    std::string clean = cleanPath(guestPath);

    // 1. If it's already a host internal sandbox path, do not remap!
    if (!config_.rootFsPath.empty() && clean.find("/com.vmgo.app/") != std::string::npos) {
        return clean;
    }

    // 2. Check virtual device node sockets
    auto devIt = virtualDevTable_.find(clean);
    if (devIt != virtualDevTable_.end()) {
        return devIt->second;
    }

    // 3. Passthrough system proc, sys, dev nodes if not virtualized
    if (clean.rfind("/proc/", 0) == 0 || clean.rfind("/sys/", 0) == 0 || clean == "/dev/null" || clean == "/dev/zero" || clean == "/dev/urandom") {
        return clean;
    }

    // 4. Resolve against mount table for /system, /vendor, /apex, /data
    for (const auto& entry : mountTable_) {
        const std::string& prefix = entry.first;
        const std::string& hostBase = entry.second;

        if (clean == prefix) {
            return hostBase;
        }

        if (clean.rfind(prefix + "/", 0) == 0) {
            std::string subPath = clean.substr(prefix.length());
            std::string target = hostBase + subPath;

            // If file or directory exists in sandbox rootfs, return sandbox path
            if (access(target.c_str(), F_OK) == 0) {
                return target;
            }

            // Fallback: If missing from sandbox, but available on host phone OS (/apex, /system/etc, /system/usr)
            if (access(clean.c_str(), F_OK) == 0) {
                return clean;
            }

            return target;
        }
    }

    // 5. Fallback: If not matched in guest mounts, return host path as-is
    return clean;
}

std::string VfsRouter::cleanPath(const std::string& path) const {
    if (path.empty()) return "";

    std::string result;
    result.reserve(path.size());

    bool lastWasSlash = false;
    for (char c : path) {
        if (c == '/') {
            if (!lastWasSlash) {
                result.push_back(c);
                lastWasSlash = true;
            }
        } else {
            result.push_back(c);
            lastWasSlash = false;
        }
    }

    if (result.size() > 1 && result.back() == '/') {
        result.pop_back();
    }

    return result;
}

void VfsRouter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    mountTable_.clear();
    virtualDevTable_.clear();
    initialized_ = false;
}

} // namespace vmgo
