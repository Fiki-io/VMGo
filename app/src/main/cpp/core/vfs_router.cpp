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

    // Priority 6: RootFS fallback
    mountTable_.emplace_back("/", config.rootFsPath);

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
    // Insert at beginning for higher priority
    mountTable_.insert(mountTable_.begin(), {guestPrefix, hostTarget});
}

std::string VfsRouter::resolvePath(const std::string& guestPath, int /* flags */) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return guestPath;
    }

    // Clean double slashes
    std::string clean = cleanPath(guestPath);

    // Check virtual dev table
    auto devIt = virtualDevTable_.find(clean);
    if (devIt != virtualDevTable_.end()) {
        return devIt->second;
    }

    // Resolve mount table
    for (const auto& entry : mountTable_) {
        const std::string& prefix = entry.first;
        const std::string& hostBase = entry.second;

        if (prefix == "/") {
            // Fallback root
            return hostBase + clean;
        }

        if (clean == prefix) {
            return hostBase;
        }

        if (clean.rfind(prefix + "/", 0) == 0) {
            std::string sub = clean.substr(prefix.length());
            return hostBase + sub;
        }
    }

    return config_.rootFsPath + clean;
}

std::string VfsRouter::cleanPath(const std::string& path) {
    if (path.empty()) return "/";
    std::string res;
    bool lastWasSlash = false;
    for (char c : path) {
        if (c == '/') {
            if (!lastWasSlash) {
                res += c;
                lastWasSlash = true;
            }
        } else {
            res += c;
            lastWasSlash = false;
        }
    }
    return res;
}

void VfsRouter::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    mountTable_.clear();
    virtualDevTable_.clear();
    initialized_ = false;
}

} // namespace vmgo
