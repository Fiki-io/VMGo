#include "seccomp_trap.h"
#include "vfs_router.h"
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/audit.h>
#include <cstddef>

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

#ifndef SECCOMP_MODE_FILTER
#define SECCOMP_MODE_FILTER 2
#endif

#ifndef AUDIT_ARCH_AARCH64
#define AUDIT_ARCH_AARCH64 (EM_AARCH64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#endif

namespace vmgo {

SeccompTrap& SeccompTrap::getInstance() {
    static SeccompTrap instance;
    return instance;
}

bool SeccompTrap::installFilter() {
    if (installed_) {
        return true;
    }

    // Step 1: Register SIGSYS signal handler
    struct sigaction sa{};
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = SeccompTrap::sigsysHandler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSYS, &sa, &oldSigsysAction_) != 0) {
        LOGE("Failed to install SIGSYS handler: %s", strerror(errno));
        return false;
    }

    // Step 2: Set PR_SET_NO_NEW_PRIVS (Mandatory for non-root seccomp)
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        LOGE("prctl(PR_SET_NO_NEW_PRIVS) failed: %s", strerror(errno));
        return false;
    }

    // Step 3: Build BPF Filter to trap openat, mknodat, mount, setuid
    struct sock_filter filter[] = {
        // [0] Load architecture
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, arch))),

        // [1] Check arch (allow if not matching to avoid bricking)
#if defined(__aarch64__)
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
#elif defined(__arm__)
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_ARM, 1, 0),
#elif defined(__x86_64__)
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_X86_64, 1, 0),
#else
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0, 1, 0),
#endif
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),

        // [3] Load syscall number
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, nr))),

#ifdef __NR_openat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_mount
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mount, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_mknodat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mknodat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_setuid
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setuid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_setgid
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setgid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

        // Default: Allow all other syscalls
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW)
    };

    struct sock_fprog prog = {
        .len = (unsigned short)(sizeof(filter) / sizeof(filter[0])),
        .filter = filter
    };

    if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog) != 0) {
        LOGW("prctl(PR_SET_SECCOMP) failed or filter unsupported, fallback active: %s", strerror(errno));
        // Continue gracefully
    } else {
        LOGI("Seccomp-BPF Syscall Filter installed successfully");
    }

    installed_ = true;
    return true;
}

void SeccompTrap::uninstall() {
    if (installed_) {
        sigaction(SIGSYS, &oldSigsysAction_, nullptr);
        installed_ = false;
        LOGI("Seccomp trap uninstalled");
    }
}

void SeccompTrap::sigsysHandler(int /* sig */, siginfo_t* info, void* context) {
    if (!info || !context) return;

    auto* uctx = reinterpret_cast<ucontext_t*>(context);
    VfsRouter& vfs = VfsRouter::getInstance();

#if defined(__aarch64__)
    // ARM64 registers:
    // x8: Syscall Number
    // x0-x5: Arguments
    // Return value in x0
    uint64_t syscallNr = uctx->uc_mcontext.regs[8];
    uint64_t arg0 = uctx->uc_mcontext.regs[0];
    uint64_t arg1 = uctx->uc_mcontext.regs[1];
    uint64_t arg2 = uctx->uc_mcontext.regs[2];
    uint64_t arg3 = uctx->uc_mcontext.regs[3];

    int64_t ret = 0;

    switch (syscallNr) {
#ifdef __NR_openat
        case __NR_openat: {
            int dirfd = static_cast<int>(arg0);
            const char* pathname = reinterpret_cast<const char*>(arg1);
            int flags = static_cast<int>(arg2);
            mode_t mode = static_cast<mode_t>(arg3);

            if (pathname) {
                std::string resolved = vfs.resolvePath(pathname, flags);
                ret = syscall(__NR_openat, dirfd, resolved.c_str(), flags, mode);
                if (ret < 0) {
                    ret = -errno;
                }
            } else {
                ret = -EFAULT;
            }
            break;
        }
#endif
#ifdef __NR_mount
        case __NR_mount: {
            // Emulate mount success in user-space sandbox
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setuid
        case __NR_setuid: {
            // Fake setuid success for sandboxed zygote
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setgid
        case __NR_setgid: {
            // Fake setgid success for sandboxed zygote
            ret = 0;
            break;
        }
#endif
        default:
            ret = -ENOSYS;
            break;
    }

    uctx->uc_mcontext.regs[0] = static_cast<uint64_t>(ret);
    // Instruction size is 4 bytes on ARM64
    uctx->uc_mcontext.pc += 4;

#elif defined(__arm__)
    uint32_t syscallNr = uctx->uc_mcontext.arm_r7;
    uint32_t arg0 = uctx->uc_mcontext.arm_r0;
    uint32_t arg1 = uctx->uc_mcontext.arm_r1;
    uint32_t arg2 = uctx->uc_mcontext.arm_r2;
    uint32_t arg3 = uctx->uc_mcontext.arm_r3;
    int32_t ret = 0;

    if (syscallNr == __NR_openat && arg1 != 0) {
        const char* pathname = reinterpret_cast<const char*>(arg1);
        std::string resolved = vfs.resolvePath(pathname, arg2);
        ret = syscall(__NR_openat, static_cast<int>(arg0), resolved.c_str(), arg2, arg3);
        if (ret < 0) ret = -errno;
    } else {
        ret = 0; // fake success for mount/setuid
    }

    uctx->uc_mcontext.arm_r0 = static_cast<uint32_t>(ret);
    uctx->uc_mcontext.arm_pc += 4;
#endif
}

} // namespace vmgo
