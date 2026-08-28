#include "seccomp_trap.h"
#include "vfs_router.h"
#include "crash_handler.h"
#include "user_kernel.h"
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include "elf_loader.h"

using namespace vmgo;

extern char** environ;

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "VMGo Loader: Usage: %s <rootfs> <target_bin> [args...]\n", argv[0]);
        return 1;
    }

    std::string rootfs = argv[1];
    std::string targetBin = argv[2];

    LOGI("VMGo Loader: Starting guest process inside sandbox: %s", rootfs.c_str());
    LOGI("VMGo Loader: Target binary: %s", targetBin.c_str());

    // 1. Install Crash Interceptor with alternate stack
    CrashHandler::install();

    // 2. Change directory into rootfs
    if (chdir(rootfs.c_str()) != 0) {
        LOGE("VMGo Loader: chdir to %s failed: %s", rootfs.c_str(), strerror(errno));
    }

    // 3. Open rootfs directory descriptor
    int rootfsDfd = open(rootfs.c_str(), O_RDONLY | O_DIRECTORY);

    // 4. Initialize UserKernel & VFS Router
    VmConfiguration config{};
    config.rootFsPath = rootfs;
    config.systemPath = rootfs + "/system";
    config.vendorPath = rootfs + "/vendor";
    config.dataPath = rootfs + "/data";
    config.apexPath = rootfs + "/apex";
    config.socketDir = rootfs + "/dev";
    config.displayWidth = 1080;
    config.displayHeight = 1920;
    config.displayDpi = 480;

    VfsRouter::getInstance().initialize(config);
    UserKernel::getInstance().initialize(config);

    // 5. Install Seccomp-BPF filter
    SeccompTrap::getInstance().installFilter(rootfsDfd);
    LOGI("VMGo Loader: Seccomp & VFS active, launching target...");

    // 6. Build argv for target
    std::vector<std::string> targetArgv;
    for (int i = 2; i < argc; ++i) {
        targetArgv.push_back(argv[i]);
    }

    std::vector<std::string> envVars;
    for (char** env = environ; env && *env; ++env) {
        envVars.push_back(*env);
    }

    // 7. Execute target binary via ElfLoader (avoids kernel execve wiping seccomp!)
    if (!ElfLoader::execute(targetBin, targetArgv, envVars, rootfs)) {
        LOGE("VMGo Loader: ElfLoader failed for %s", targetBin.c_str());
        return 127;
    }

    return 0;
}
