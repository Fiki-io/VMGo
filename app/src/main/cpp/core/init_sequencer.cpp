#include "init_sequencer.h"
#include "elf_loader.h"
#include "seccomp_trap.h"
#include "user_kernel.h"
#include "vfs_router.h"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <cstring>
#include <cerrno>

extern char** environ;

namespace vmgo {

InitSequencer& InitSequencer::getInstance() {
    static InitSequencer instance;
    return instance;
}

bool InitSequencer::startSequence(const VmConfiguration& config, LogCallback /* onLog */) {
    LOGI("InitSequencer: Starting multi-stage daemon boot sequence for slot %s...", config.slotId.c_str());

    std::string rootfs = config.rootFsPath;
    running_ = true;

    // 1. Check candidate daemons
    std::string serviceMgrBin = rootfs + "/system/bin/servicemanager";
    std::string hwServiceMgrBin = rootfs + "/system/bin/hwservicemanager";
    std::string surfaceFlingerBin = rootfs + "/system/bin/surfaceflinger";
    std::string zygoteBin = rootfs + "/system/bin/app_process64";
    if (access(zygoteBin.c_str(), F_OK) != 0) {
        zygoteBin = rootfs + "/system/bin/app_process";
    }

    std::string initBin = rootfs + "/init";
    if (access(initBin.c_str(), F_OK) != 0) {
        initBin = rootfs + "/system/bin/init";
    }

    // Stage 1: Start servicemanager (Binder Master)
    if (access(serviceMgrBin.c_str(), F_OK) == 0) {
        LOGI("InitSequencer: Stage 1 - Spawning servicemanager...");
        startDaemon(serviceMgrBin, { serviceMgrBin }, rootfs, "servicemanager");
        usleep(100000); // 100ms grace period
    } else {
        LOGI("InitSequencer: servicemanager binary not found, using internal virtual binder provider");
    }

    // Stage 2: Start hwservicemanager (HIDL Master) if available
    if (access(hwServiceMgrBin.c_str(), F_OK) == 0) {
        LOGI("InitSequencer: Stage 1b - Spawning hwservicemanager...");
        startDaemon(hwServiceMgrBin, { hwServiceMgrBin }, rootfs, "hwservicemanager");
        usleep(50000);
    }

    // Stage 3: Start surfaceflinger (Compositor via QEMU Pipe)
    if (access(surfaceFlingerBin.c_str(), F_OK) == 0) {
        LOGI("InitSequencer: Stage 2 - Spawning surfaceflinger...");
        startDaemon(surfaceFlingerBin, { surfaceFlingerBin }, rootfs, "surfaceflinger");
        usleep(100000);
    }

    // Stage 4: Start main Zygote / init
    std::vector<std::string> zygoteArgs;
    std::string mainTarget;

    if (access(initBin.c_str(), F_OK) == 0) {
        mainTarget = initBin;
        zygoteArgs = {
            initBin,
            std::to_string(config.displayWidth),
            std::to_string(config.displayHeight),
            std::to_string(config.displayDpi),
            "0",
            "02:00:00:00:00:01",
            "vmgo0001",
            "VMGo",
            "2",
            rootfs,
            "31"
        };
    } else {
        mainTarget = zygoteBin;
        zygoteArgs = {
            zygoteBin,
            "-Xzygote",
            "/system/bin",
            "--zygote",
            "--start-system-server"
        };
    }

    LOGI("InitSequencer: Stage 3 - Spawning primary runtime: %s...", mainTarget.c_str());
    bool ok = startDaemon(mainTarget, zygoteArgs, rootfs, "zygote/init");

    LOGI("InitSequencer: Boot sequence initialized with %zu managed processes", daemonPids_.size());
    return ok;
}

bool InitSequencer::startDaemon(
    const std::string& binaryPath,
    const std::vector<std::string>& args,
    const std::string& rootfs,
    const std::string& name
) {
    pid_t pid = fork();
    if (pid < 0) {
        LOGE("InitSequencer: Failed to fork for %s: %s", name.c_str(), strerror(errno));
        return false;
    }

    if (pid == 0) {
        // CHILD PROCESS (Daemon)
        if (chdir(rootfs.c_str()) != 0) {
            // chdir notice
        }

        // Install Seccomp-BPF filter on daemon process
        int rootfsDfd = open(rootfs.c_str(), O_RDONLY | O_DIRECTORY);
        SeccompTrap::getInstance().installFilter(rootfsDfd);

        std::vector<std::string> envList;
        for (char** env = environ; *env != nullptr; ++env) {
            envList.emplace_back(*env);
        }

        bool started = ElfLoader::execute(binaryPath, args, envList, rootfs);
        if (!started) {
            std::vector<const char*> cArgs;
            for (const auto& a : args) cArgs.push_back(a.c_str());
            cArgs.push_back(nullptr);
            execve(binaryPath.c_str(), const_cast<char* const*>(cArgs.data()), environ);
        }

        _exit(127);
    }

    // PARENT PROCESS
    daemonPids_.push_back(pid);
    LOGI("InitSequencer: Daemon [%s] running with PID %d", name.c_str(), pid);
    return true;
}

void InitSequencer::stop() {
    running_ = false;
    for (pid_t pid : daemonPids_) {
        if (pid > 0) {
            kill(pid, SIGTERM);
            usleep(20000);
            kill(pid, SIGKILL);
        }
    }
    daemonPids_.clear();
    LOGI("InitSequencer: All daemons stopped");
}

} // namespace vmgo
