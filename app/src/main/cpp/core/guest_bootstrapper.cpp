#include "guest_bootstrapper.h"
#include "seccomp_trap.h"
#include "vfs_router.h"
#include "elf_loader.h"
#include "user_kernel.h"
#include "init_sequencer.h"
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

static void mkdirRecursive(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/' || i == path.size() - 1) {
            mkdir(current.c_str(), 0755);
        }
    }
}

GuestBootstrapper& GuestBootstrapper::getInstance() {
    static GuestBootstrapper instance;
    return instance;
}

void GuestBootstrapper::createVirtualDevNodes(const std::string& rootfs) {
    UserKernel::getInstance().setupVirtualDevNodes(rootfs);
    setupTimezoneData(rootfs);
    LOGI("Guest bootstrap: Virtual dev nodes and UserKernel initialized in %s", rootfs.c_str());
}

void GuestBootstrapper::setupTimezoneData(const std::string& rootfs) {
    std::string apexTzDir = rootfs + "/apex/com.android.tzdata/etc/tz";
    std::string sysTzDir = rootfs + "/system/usr/share/zoneinfo";
    std::string sysEtcTz = rootfs + "/system/etc/tz";
    std::string runtimeTzDir = rootfs + "/apex/com.android.runtime/etc/tz";

    mkdirRecursive(apexTzDir);
    mkdirRecursive(sysTzDir);
    mkdirRecursive(sysEtcTz);
    mkdirRecursive(runtimeTzDir);

    // List of tz files needed by bionic
    std::vector<std::string> tzFiles = { "tzdata", "tzlookup.xml", "tz_version", "telephonylookup.xml" };

    // Host locations where tz files might exist
    std::vector<std::string> hostSearchDirs = {
        "/apex/com.android.tzdata/etc/tz",
        "/apex/com.android.runtime/etc/tz",
        "/system/usr/share/zoneinfo",
        "/system/etc/tz"
    };

    for (const auto& fname : tzFiles) {
        for (const auto& hDir : hostSearchDirs) {
            std::string hFile = hDir + "/" + fname;
            if (access(hFile.c_str(), R_OK) == 0) {
                // Ensure symlinks exist in all candidate guest directories
                std::vector<std::string> targets = {
                    apexTzDir + "/" + fname,
                    sysTzDir + "/" + fname,
                    sysEtcTz + "/" + fname,
                    runtimeTzDir + "/" + fname
                };
                for (const auto& tgt : targets) {
                    if (access(tgt.c_str(), F_OK) != 0) {
                        symlink(hFile.c_str(), tgt.c_str());
                    }
                }
                break;
            }
        }
    }
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
    setenv("VMGO_DPI", std::to_string(config.displayDpi).c_str(), 1);

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
    (void)onLog;
    if (running_ && guestPid_ > 0) {
        LOGW("Guest already running with PID %d", guestPid_);
        return true;
    }

    LOGI("Guest bootstrap: Preparing environment for slot %s...", config.slotId.c_str());

    UserKernel::getInstance().initialize(config);

    std::string rootfs = config.rootFsPath;
    std::string systemPath = config.systemPath;

    // List of candidate boot binaries in priority order
    std::vector<std::string> candidates = {
        rootfs + "/init",
        rootfs + "/system/bin/app_process64",
        rootfs + "/system/bin/app_process",
        systemPath + "/bin/app_process64",
        systemPath + "/bin/app_process",
        rootfs + "/system/bin/sh",
        rootfs + "/bin/sh",
        systemPath + "/bin/sh"
    };

    std::string initBin;
    for (const auto& candidate : candidates) {
        if (access(candidate.c_str(), F_OK) == 0) {
            chmod(candidate.c_str(), 0755);
            if (access(candidate.c_str(), X_OK) == 0) {
                initBin = candidate;
                break;
            }
        }
    }

    if (initBin.empty()) {
        LOGE("Guest bootstrap: No executable init or app_process found in %s or %s",
             rootfs.c_str(), systemPath.c_str());
        
        // List what IS in rootfs and systemPath for debugging
        LOGI("Guest bootstrap: Listing rootfs (%s):", rootfs.c_str());
        DIR* dir = opendir(rootfs.c_str());
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                LOGI("  rootfs/%s (%s)", entry->d_name,
                     entry->d_type == DT_DIR ? "dir" : "file");
            }
            closedir(dir);
        }

        LOGI("Guest bootstrap: Listing systemPath (%s):", systemPath.c_str());
        DIR* sDir = opendir(systemPath.c_str());
        if (sDir) {
            struct dirent* entry;
            while ((entry = readdir(sDir)) != nullptr) {
                LOGI("  system/%s (%s)", entry->d_name,
                     entry->d_type == DT_DIR ? "dir" : "file");
            }
            closedir(sDir);
        }
        return false;
    }

    LOGI("Guest bootstrap: Using init binary: %s", initBin.c_str());

    // Ensure all companion binaries and linkers are executable
    chmod(initBin.c_str(), 0755);
    chmod((rootfs + "/system/bin/linker64").c_str(), 0755);
    chmod((rootfs + "/system/bin/linker").c_str(), 0755);
    chmod((systemPath + "/bin/linker64").c_str(), 0755);
    chmod((systemPath + "/bin/linker").c_str(), 0755);
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

        // Open rootfs directory descriptor for clean BPF bypass and sandbox scoping
        int rootfsDfd = open(rootfs.c_str(), O_RDONLY | O_DIRECTORY);

        // Install Seccomp-BPF filter on THIS child process
        // All syscalls from this process will be intercepted by our trap handler
        SeccompTrap::getInstance().installFilter(rootfsDfd);

        // Build argv based on what binary we found
        std::vector<const char*> argv;
        std::string widthStr = std::to_string(config.displayWidth);
        std::string heightStr = std::to_string(config.displayHeight);
        std::string dpiStr = std::to_string(config.displayDpi);

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

        LOGI("Guest bootstrap: Launching %s (%zu args)...", initBin.c_str(), argv.size() - 1);

        // 1. Launch via User-Space ELF Loader (In-Memory execution bypassing Android 10+ SELinux execve restrictions)
        std::vector<std::string> argsList;
        for (size_t i = 0; i < argv.size() - 1; ++i) {
            if (argv[i]) argsList.emplace_back(argv[i]);
        }

        std::vector<std::string> envList;
        for (char** env = environ; *env != nullptr; ++env) {
            envList.emplace_back(*env);
        }

        bool loaderStarted = ElfLoader::execute(initBin, argsList, envList, rootfs);
        if (!loaderStarted) {
            // 2. Fallback to standard execve
            LOGW("Guest bootstrap: ElfLoader failed to start, attempting execve fallback...");
            execve(initBin.c_str(), const_cast<char* const*>(argv.data()), environ);
        }

        // If we reach here, both ElfLoader and execve failed
        fprintf(stderr, "Guest: Launch FAILED for %s: %s (errno=%d)\n",
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
    InitSequencer::getInstance().stop();
    UserKernel::getInstance().reset();
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
