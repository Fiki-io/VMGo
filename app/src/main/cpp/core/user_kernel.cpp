#include "user_kernel.h"
#include "virtual_binder.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

namespace vmgo {

static void mkdirRecursive(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            mkdir(current.c_str(), 0755);
        }
    }
}

static void touchFile(const std::string& path) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_NOCTTY | O_NONBLOCK, 0666);
    if (fd >= 0) close(fd);
}

UserKernel& UserKernel::getInstance() {
    static UserKernel instance;
    return instance;
}

void UserKernel::initialize(const VmConfiguration& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    initialized_ = true;

    // Initialize default Android system properties
    properties_["ro.hardware"] = "vmgo";
    properties_["ro.boot.hardware"] = "vmgo";
    properties_["ro.kernel.qemu"] = "1";
    properties_["qemu.gles"] = "1";
    properties_["qemu.sf.lcd_density"] = std::to_string(config.displayDpi);
    properties_["ro.sf.lcd_density"] = std::to_string(config.displayDpi);
    properties_["ro.serialno"] = "vmgo0001";
    properties_["ro.boot.serialno"] = "vmgo0001";
    properties_["ro.product.device"] = "vmgo";
    properties_["ro.product.model"] = "VMGo Virtual Phone";
    properties_["ro.build.version.release"] = "11";
    properties_["ro.build.version.sdk"] = "30";
    properties_["init.svc.servicemanager"] = "running";
    properties_["init.svc.surfaceflinger"] = "running";
    properties_["init.svc.zygote"] = "running";

    // Initialize virtual binder subsystem
    VirtualBinder::getInstance().initialize(config.rootFsPath + "/dev");

    LOGI("UserKernel: Initialized with hardware: %s, density: %d",
         properties_["ro.hardware"].c_str(), config.displayDpi);
}

void UserKernel::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    VirtualBinder::getInstance().reset();
    properties_.clear();
    initialized_ = false;
    LOGI("UserKernel: Reset");
}

int UserKernel::sysMount(
    const char* source,
    const char* target,
    const char* filesystemtype,
    unsigned long /* mountflags */,
    const void* /* data */
) {
    LOGI("UserKernel: sys_mount(%s -> %s, type=%s) -> EMULATED SUCCESS",
         source ? source : "none", target ? target : "none", filesystemtype ? filesystemtype : "none");
    if (target) {
        mkdirRecursive(target);
    }
    return 0;
}

int UserKernel::sysUmount(const char* target, int /* flags */) {
    LOGI("UserKernel: sys_umount(%s) -> EMULATED SUCCESS", target ? target : "none");
    return 0;
}

int UserKernel::sysChroot(const char* path) {
    LOGI("UserKernel: sys_chroot(%s) -> EMULATED SUCCESS", path ? path : "none");
    if (path) {
        chdir(path);
    }
    return 0;
}

int UserKernel::sysPivotRoot(const char* newRoot, const char* putOld) {
    LOGI("UserKernel: sys_pivot_root(%s, %s) -> EMULATED SUCCESS",
         newRoot ? newRoot : "none", putOld ? putOld : "none");
    if (newRoot) {
        chdir(newRoot);
    }
    return 0;
}

int UserKernel::sysMknodat(int /* dirfd */, const char* pathname, mode_t /* mode */, dev_t /* dev */) {
    LOGI("UserKernel: sys_mknodat(%s) -> EMULATED SUCCESS", pathname ? pathname : "none");
    if (pathname) {
        touchFile(pathname);
    }
    return 0;
}

void UserKernel::setProperty(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    properties_[key] = value;
}

std::string UserKernel::getProperty(const std::string& key, const std::string& defaultVal) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = properties_.find(key);
    if (it != properties_.end()) {
        return it->second;
    }
    return defaultVal;
}

void UserKernel::setupVirtualDevNodes(const std::string& rootfs) {
    // 1. Create sandbox directory structure
    std::vector<std::string> dirs = {
        rootfs + "/dev",
        rootfs + "/dev/socket",
        rootfs + "/dev/input",
        rootfs + "/dev/graphics",
        rootfs + "/proc",
        rootfs + "/sys",
        rootfs + "/sys/fs/selinux",
        rootfs + "/data",
        rootfs + "/data/dalvik-cache",
        rootfs + "/data/dalvik-cache/arm64",
        rootfs + "/data/system",
        rootfs + "/data/misc",
        rootfs + "/data/data",
        rootfs + "/data/local/tmp",
        rootfs + "/cache",
        rootfs + "/mnt",
        rootfs + "/mnt/runtime",
        rootfs + "/apex"
    };

    for (const auto& d : dirs) {
        mkdirRecursive(d);
    }

    // 2. Create Virtual Binder Nodes
    touchFile(rootfs + "/dev/binder");
    touchFile(rootfs + "/dev/vndbinder");
    touchFile(rootfs + "/dev/hwbinder");

    // 3. Create Hardware & Memory dev nodes
    touchFile(rootfs + "/dev/ashmem");
    touchFile(rootfs + "/dev/ion");
    touchFile(rootfs + "/dev/fuse");
    touchFile(rootfs + "/dev/tty");
    touchFile(rootfs + "/dev/ptmx");
    touchFile(rootfs + "/dev/null");
    touchFile(rootfs + "/dev/zero");
    touchFile(rootfs + "/dev/urandom");

    // 4. Create QEMU Pipe socket link
    std::string qemuPipeSock = rootfs + "/dev/qemu_pipe.sock";
    unlink((rootfs + "/dev/qemu_pipe").c_str());
    symlink(qemuPipeSock.c_str(), (rootfs + "/dev/qemu_pipe").c_str());

    // 5. Create proc/cmdline
    touchFile(rootfs + "/proc/cmdline");
    int cmdFd = open((rootfs + "/proc/cmdline").c_str(), O_WRONLY | O_TRUNC);
    if (cmdFd >= 0) {
        const char* cmdline = "androidboot.hardware=vmgo androidboot.selinux=permissive "
                              "no_timer_check skip_initramfs ro init=/init "
                              "qemu.dalvik.vm.heapsize=256m androidboot.serialno=vmgo0001\n";
        write(cmdFd, cmdline, strlen(cmdline));
        close(cmdFd);
    }

    // 6. Set SELinux to permissive
    int seFd = open((rootfs + "/sys/fs/selinux/enforce").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (seFd >= 0) {
        write(seFd, "0", 1);
        close(seFd);
    }

    LOGI("UserKernel: Virtual dev nodes and Binder created in %s", rootfs.c_str());
}

} // namespace vmgo
