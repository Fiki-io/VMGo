#include "guest_bootstrapper.h"
#include "seccomp_trap.h"
#include "vfs_router.h"
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <thread>
#include <vector>

namespace vmgo {

GuestBootstrapper& GuestBootstrapper::getInstance() {
    static GuestBootstrapper instance;
    return instance;
}

void GuestBootstrapper::createVirtualDevNodes(const std::string& rootfs) {
    // Create necessary directory structure inside the sandbox
    std::vector<std::string> dirs = {
        rootfs + "/dev",
        rootfs + "/dev/socket",
        rootfs + "/dev/input",
        rootfs + "/proc",
        rootfs + "/sys",
        rootfs + "/sys/fs",
        rootfs + "/sys/fs/selinux",
        rootfs + "/data",
        rootfs + "/data/dalvik-cache",
        rootfs + "/data/dalvik-cache/arm64",
        rootfs + "/data/app",
        rootfs + "/data/data",
        rootfs + "/data/system",
        rootfs + "/data/misc",
        rootfs + "/data/local",
        rootfs + "/data/local/tmp",
        rootfs + "/cache",
        rootfs + "/mnt",
        rootfs + "/mnt/runtime",
        rootfs + "/storage",
        rootfs + "/acct",
        rootfs + "/config",
        rootfs + "/oem",
        rootfs + "/metadata",
    };

    for (const auto& dir : dirs) {
        mkdir(dir.c_str(), 0755);
    }

    // Create virtual device files (regular files, not real device nodes)
    // Our seccomp-BPF will intercept all ioctl/read/write on these
    auto touchFile = [](const std::string& path) {
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0) close(fd);
    };

    touchFile(rootfs + "/dev/null");
    touchFile(rootfs + "/dev/zero");
    touchFile(rootfs + "/dev/random");
    touchFile(rootfs + "/dev/urandom");
    touchFile(rootfs + "/dev/binder");
    touchFile(rootfs + "/dev/hwbinder");
    touchFile(rootfs + "/dev/vndbinder");
    touchFile(rootfs + "/dev/ashmem");
    touchFile(rootfs + "/dev/ion");
    touchFile(rootfs + "/dev/fuse");
    touchFile(rootfs + "/dev/tty");
    touchFile(rootfs + "/dev/ptmx");

    // Create /dev/qemu_pipe as a symlink to our QEMU Pipe socket
    std::string qemuPipeSock = rootfs + "/dev/qemu_pipe.sock";
    unlink((rootfs + "/dev/qemu_pipe").c_str());
    symlink(qemuPipeSock.c_str(), (rootfs + "/dev/qemu_pipe").c_str());

    // Create proc stubs that init may read
    touchFile(rootfs + "/proc/cmdline");
    
    // Write a fake cmdline that guest init expects
    int cmdFd = open((rootfs + "/proc/cmdline").c_str(), O_WRONLY | O_TRUNC);
    if (cmdFd >= 0) {
        const char* cmdline = "androidboot.hardware=vmgo androidboot.selinux=permissive "
                              "no_timer_check skip_initramfs ro init=/init "
                              "qemu.dalvik.vm.heapsize=256m\n";
        write(cmdFd, cmdline, strlen(cmdline));
        close(cmdFd);
    }

    // SELinux enforce file (set to permissive)
    int seFd = open((rootfs + "/sys/fs/selinux/enforce").c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (seFd >= 0) {
        write(seFd, "0", 1);
        close(seFd);
    }

    // Create build.prop if not exists
    std::string buildProp = rootfs + "/system/build.prop";
    if (access(buildProp.c_str(), F_OK) != 0) {
        buildProp = rootfs + "/build.prop";
    }

    LOGI("Guest bootstrap: Virtual dev nodes created in %s", rootfs.c_str());
}

void GuestBootstrapper::setupEnvironment(const VmConfiguration& config) {
    std::string rootfs = config.rootFsPath;

    // Core Android environment
    setenv("ANDROID_ROOT", (rootfs + "/system").c_str(), 1);
    setenv("ANDROID_DATA", (rootfs + "/data").c_str(), 1);
    setenv("ANDROID_STORAGE", (rootfs + "/storage").c_str(), 1);
    setenv("ANDROID_ART_ROOT", (rootfs + "/apex/com.android.art").c_str(), 1);
    setenv("ANDROID_I18N_ROOT", (rootfs + "/apex/com.android.i18n").c_str(), 1);
    setenv("ANDROID_TZDATA_ROOT", (rootfs + "/apex/com.android.tzdata").c_str(), 1);
    setenv("ANDROID_RUNTIME_ROOT", (rootfs + "/apex/com.android.runtime").c_str(), 1);
    setenv("DEX2OATBOOTCLASSPATH", "", 1);
    setenv("BOOTCLASSPATH", "", 1);

    // Library search paths
    std::string ldPath = rootfs + "/system/lib64:" +
                         rootfs + "/system/lib:" +
                         rootfs + "/vendor/lib64:" +
                         rootfs + "/vendor/lib:" +
                         rootfs + "/apex/com.android.art/lib64:" +
                         rootfs + "/apex/com.android.runtime/lib64";
    setenv("LD_LIBRARY_PATH", ldPath.c_str(), 1);

    // Binary paths
    std::string path = rootfs + "/system/bin:" +
                       rootfs + "/system/xbin:" +
                       rootfs + "/vendor/bin";
    setenv("PATH", path.c_str(), 1);

    // VM-specific
    setenv("VMGO_SLOT_ID", config.slotId.c_str(), 1);
    setenv("VMGO_DISPLAY_WIDTH", std::to_string(config.displayWidth).c_str(), 1);
    setenv("VMGO_DISPLAY_HEIGHT", std::to_string(config.displayHeight).c_str(), 1);
    setenv("VMGO_DPI", std::to_string(config.dpi).c_str(), 1);

    // Hardware identifiers
    setenv("ANDROID_BOOTLOGO", "1", 1);
    setenv("EXTERNAL_STORAGE", (rootfs + "/sdcard").c_str(), 1);

    LOGI("Guest bootstrap: Environment configured for slot %s", config.slotId.c_str());
}

void GuestBootstrapper::startLogReader(int pipeFd) {
    std::thread([pipeFd]() {
        char buf[4096];
        ssize_t n;
        while ((n = read(pipeFd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            // Log each line from guest stdout/stderr
            char* line = strtok(buf, "\n");
            while (line) {
                LOGI("Guest: %s", line);
                line = strtok(nullptr, "\n");
            }
        }
        close(pipeFd);
    }).detach();
}

bool GuestBootstrapper::launch(const VmConfiguration& config, LogCallback onLog) {
    if (running_ && guestPid_ > 0) {
        LOGW("Guest already running with PID %d", guestPid_);
        return true;
    }

    std::string rootfs = config.rootFsPath;

    // Check if init or app_process64 exists
    std::string initBin;
    if (access((rootfs + "/init").c_str(), X_OK) == 0) {
        initBin = rootfs + "/init";
    } else if (access((rootfs + "/system/bin/app_process64").c_str(), X_OK) == 0) {
        initBin = rootfs + "/system/bin/app_process64";
    } else if (access((rootfs + "/system/bin/app_process").c_str(), X_OK) == 0) {
        initBin = rootfs + "/system/bin/app_process";
    } else {
        LOGE("Guest bootstrap: No executable init or app_process found in %s", rootfs.c_str());
        LOGE("Guest bootstrap: Checked: %s/init, %s/system/bin/app_process64", rootfs.c_str(), rootfs.c_str());
        
        // List what IS in the rootfs for debugging
        LOGI("Guest bootstrap: Listing rootfs contents:");
        DIR* dir = opendir(rootfs.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                LOGI("  rootfs/%s (%s)", entry->d_name,
                     entry->d_type == DT_DIR ? "dir" : "file");
            }
            closedir(dir);
        }
        return false;
    }

    LOGI("Guest bootstrap: Using init binary: %s", initBin.c_str());

    // Ensure init is executable
    chmod(initBin.c_str(), 0755);
    if (access((rootfs + "/system/bin/app_process64").c_str(), F_OK) == 0) {
        chmod((rootfs + "/system/bin/app_process64").c_str(), 0755);
    }
    if (access((rootfs + "/system/bin/linker64").c_str(), F_OK) == 0) {
        chmod((rootfs + "/system/bin/linker64").c_str(), 0755);
    }

    // Create virtual /dev/ structure
    createVirtualDevNodes(rootfs);

    // Create pipe for capturing guest stdout/stderr
    int pipeFds[2];
    if (pipe2(pipeFds, O_CLOEXEC) != 0) {
        LOGE("Guest bootstrap: Failed to create log pipe");
        return false;
    }

    LOGI("Guest bootstrap: Forking child process...");

    pid_t pid = fork();
    if (pid < 0) {
        LOGE("Guest bootstrap: fork() failed: %s", strerror(errno));
        close(pipeFds[0]);
        close(pipeFds[1]);
        return false;
    }

    if (pid == 0) {
        // ============ CHILD PROCESS ============
        // This IS the guest. Everything from here runs as the guest OS.

        // Redirect stdout/stderr to pipe
        close(pipeFds[0]); // Close read end
        dup2(pipeFds[1], STDOUT_FILENO);
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[1]);

        // Setup environment
        setupEnvironment(config);

        // Change working directory to rootfs
        if (chdir(rootfs.c_str()) != 0) {
            fprintf(stderr, "Guest: chdir to %s failed: %s\n", rootfs.c_str(), strerror(errno));
        }

        // Install Seccomp-BPF filter on THIS child process
        // All syscalls from this process will be intercepted by our trap handler
        SeccompTrap::getInstance().install(config);

        // Build argv based on what binary we found
        std::vector<const char*> argv;
        std::string widthStr = std::to_string(config.displayWidth);
        std::string heightStr = std::to_string(config.displayHeight);
        std::string dpiStr = std::to_string(config.dpi);

        if (initBin.find("app_process") != std::string::npos) {
            // Launch as zygote (like VMOS does with app_process64)
            argv = {
                initBin.c_str(),
                "-Xzygote",
                "/system/bin",
                "--zygote",
                "--start-system-server",
                nullptr
            };
        } else {
            // Launch custom init (like VMOS's custom init binary)
            // Parameters from RE: /init <width> <height> <dpi> <navH> <mac> <serial> <engine> <hal> <sandbox> <api>
            argv = {
                initBin.c_str(),
                widthStr.c_str(),
                heightStr.c_str(),
                dpiStr.c_str(),
                "0",
                "02:00:00:00:00:01",
                "vmgo0001",
                "VMGo",
                "2",
                rootfs.c_str(),
                "31",
                nullptr
            };
        }

        LOGI("Guest bootstrap: execve(%s) with %zu args", initBin.c_str(), argv.size() - 1);

        // EXEC! This replaces the child process with the guest init
        execve(initBin.c_str(), const_cast<char* const*>(argv.data()), environ);

        // If we reach here, execve failed
        fprintf(stderr, "Guest: execve(%s) FAILED: %s (errno=%d)\n",
                initBin.c_str(), strerror(errno), errno);
        _exit(127);
    }

    // ============ PARENT PROCESS ============
    close(pipeFds[1]); // Close write end
    guestPid_ = pid;
    running_ = true;
    logPipeFd_ = pipeFds[0];

    LOGI("Guest bootstrap: Child process forked successfully with PID %d", pid);

    // Start background thread to read guest output
    startLogReader(pipeFds[0]);

    // Start monitoring thread
    std::thread([this]() {
        int status = 0;
        pid_t result = waitpid(guestPid_, &status, 0);
        if (result > 0) {
            if (WIFEXITED(status)) {
                LOGI("Guest process PID %d exited with code %d", guestPid_, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                LOGE("Guest process PID %d killed by signal %d", guestPid_, WTERMSIG(status));
            }
        }
        running_ = false;
        guestPid_ = -1;
    }).detach();

    return true;
}

bool GuestBootstrapper::isAlive() const {
    if (guestPid_ <= 0) return false;
    return ::kill(guestPid_, 0) == 0;
}

void GuestBootstrapper::kill() {
    if (guestPid_ > 0) {
        LOGI("Guest bootstrap: Killing guest PID %d", guestPid_);
        ::kill(guestPid_, SIGKILL);
        int status;
        waitpid(guestPid_, &status, WNOHANG);
        guestPid_ = -1;
        running_ = false;
    }
}

} // namespace vmgo
