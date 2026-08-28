#include "seccomp_trap.h"
#include "vfs_router.h"
#include "virtual_binder.h"
#include "user_kernel.h"
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
#include <elf.h>
#include <vector>
#include <string>
#include "elf_loader.h"

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

#ifndef SECCOMP_MODE_FILTER
#define SECCOMP_MODE_FILTER 2
#endif

#ifndef AUDIT_ARCH_AARCH64
#define AUDIT_ARCH_AARCH64 (EM_AARCH64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#endif

#ifndef AUDIT_ARCH_ARM
#define AUDIT_ARCH_ARM (EM_ARM|__AUDIT_ARCH_LE)
#endif

#ifndef AUDIT_ARCH_X86_64
#define AUDIT_ARCH_X86_64 (EM_X86_64|__AUDIT_ARCH_64BIT|__AUDIT_ARCH_LE)
#endif

#define SECCOMP_BYPASS_MAGIC 0x7FFFFFFF

namespace vmgo {

SeccompTrap& SeccompTrap::getInstance() {
    static SeccompTrap instance;
    return instance;
}

bool SeccompTrap::installFilter(int rootfsDfd) {
    if (installed_) {
        return true;
    }
    rootfsDfd_ = rootfsDfd;

    // Step 1: Register SIGSYS signal handler with SA_SIGINFO
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

    uint32_t bypassDfd = (rootfsDfd_ >= 0) ? static_cast<uint32_t>(rootfsDfd_) : SECCOMP_BYPASS_MAGIC;

    // Step 3: Build BPF Filter with bypass token check to avoid recursive trap
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
        // Check if syscall is openat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_openat, 0, 4),
        // If openat, check if arg0 == bypassDfd (Internal handler call -> ALLOW)
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, args[0]))),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, bypassDfd, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        // Otherwise, TRAP to SIGSYS
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_faccessat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_faccessat, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, args[0]))),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, bypassDfd, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_newfstatat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_newfstatat, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, args[0]))),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, bypassDfd, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_execveat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execveat, 0, 4),
        BPF_STMT(BPF_LD | BPF_W | BPF_ABS, (offsetof(struct seccomp_data, args[0]))),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, bypassDfd, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_execve
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_execve, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_mount
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mount, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_umount2
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_umount2, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_pivot_root
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_pivot_root, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_chroot
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_chroot, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_mknodat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mknodat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_ioctl
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_ioctl, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_readlinkat
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_readlinkat, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_mknod
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_mknod, 0, 1),
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

#ifdef __NR_setreuid
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setreuid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_setregid
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setregid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_setresuid
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setresuid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_setresgid
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setresgid, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_setgroups
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setgroups, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_capset
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_capset, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_sethostname
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_sethostname, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_setdomainname
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_setdomainname, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_reboot
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_reboot, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_swapon
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_swapon, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
#endif

#ifdef __NR_swapoff
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_swapoff, 0, 1),
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
        LOGW("prctl(PR_SET_SECCOMP) filter notice: %s", strerror(errno));
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
    uint64_t syscallNr = uctx->uc_mcontext.regs[8];
    uint64_t arg0 = uctx->uc_mcontext.regs[0];
    uint64_t arg1 = uctx->uc_mcontext.regs[1];
    uint64_t arg2 = uctx->uc_mcontext.regs[2];
    uint64_t arg3 = uctx->uc_mcontext.regs[3];
    (void)arg0;

    int dfd = (SeccompTrap::getInstance().rootfsDfd_ >= 0) ? SeccompTrap::getInstance().rootfsDfd_ : AT_FDCWD;
    int bypassDfd = (SeccompTrap::getInstance().rootfsDfd_ >= 0) ? SeccompTrap::getInstance().rootfsDfd_ : SECCOMP_BYPASS_MAGIC;
    int64_t ret = 0;

    switch (syscallNr) {
#ifdef __NR_openat
        case __NR_openat: {
            const char* pathname = reinterpret_cast<const char*>(arg1);
            int flags = static_cast<int>(arg2);
            mode_t mode = static_cast<mode_t>(arg3);

            if (pathname) {
                std::string resolved = vfs.resolvePath(pathname, flags);
                int targetDfd = (resolved.empty() || resolved[0] == '/') ? AT_FDCWD : dfd;
                ret = syscall(__NR_openat, targetDfd, resolved.c_str(), flags, mode);
                if (ret >= 0) {
                    if (resolved.find("/dev/binder") != std::string::npos ||
                        resolved.find("/dev/vndbinder") != std::string::npos ||
                        resolved.find("/dev/hwbinder") != std::string::npos) {
                        VirtualBinder::getInstance().registerBinderFd(static_cast<int>(ret), resolved);
                    }
                } else {
                    ret = -errno;
                }
                LOGI("Guest Syscall: openat(%s) -> %s [fd=%ld]", pathname, resolved.c_str(), (long)ret);
            } else {
                ret = -EFAULT;
            }
            break;
        }
#endif
#ifdef __NR_ioctl
        case __NR_ioctl: {
            int targetFd = static_cast<int>(arg0);
            unsigned long request = static_cast<unsigned long>(arg1);
            void* argp = reinterpret_cast<void*>(arg2);

            if (VirtualBinder::getInstance().isBinderFd(targetFd)) {
                ret = VirtualBinder::getInstance().handleIoctl(targetFd, request, argp);
                LOGI("Guest Syscall: ioctl(Binder fd=%d, req=0x%lx) -> %ld", targetFd, request, (long)ret);
            } else {
                ret = syscall(__NR_ioctl, targetFd, request, argp);
                if (ret < 0) ret = -errno;
            }
            break;
        }
#endif
#ifdef __NR_faccessat
        case __NR_faccessat: {
            const char* pathname = reinterpret_cast<const char*>(arg1);
            int mode = static_cast<int>(arg2);
            int flags = static_cast<int>(arg3);

            if (pathname) {
                std::string resolved = vfs.resolvePath(pathname, 0);
                int targetDfd = (resolved.empty() || resolved[0] == '/') ? AT_FDCWD : dfd;
                ret = syscall(__NR_faccessat, targetDfd, resolved.c_str(), mode, flags);
                if (ret < 0) {
                    ret = -errno;
                }
            } else {
                ret = -EFAULT;
            }
            break;
        }
#endif
#ifdef __NR_newfstatat
        case __NR_newfstatat: {
            const char* pathname = reinterpret_cast<const char*>(arg1);
            void* statbuf = reinterpret_cast<void*>(arg2);
            int flags = static_cast<int>(arg3);

            if (pathname) {
                std::string resolved = vfs.resolvePath(pathname, 0);
                int targetDfd = (resolved.empty() || resolved[0] == '/') ? AT_FDCWD : dfd;
                ret = syscall(__NR_newfstatat, targetDfd, resolved.c_str(), statbuf, flags);
                if (ret < 0) {
                    ret = -errno;
                }
            } else {
                ret = -EFAULT;
            }
            break;
        }
#endif
#ifdef __NR_execveat
        case __NR_execveat:
#endif
#ifdef __NR_execve
        case __NR_execve: {
            const char* pathname;
            char* const* guest_argv;
            char* const* guest_envp;
            
            if (syscallNr == __NR_execve) {
                pathname = reinterpret_cast<const char*>(arg0);
                guest_argv = reinterpret_cast<char* const*>(arg1);
                guest_envp = reinterpret_cast<char* const*>(arg2);
            } else {
                pathname = reinterpret_cast<const char*>(arg1);
                guest_argv = reinterpret_cast<char* const*>(arg2);
                guest_envp = reinterpret_cast<char* const*>(arg3);
            }
            
            if (pathname) {
                std::string resolvedPath = vfs.resolvePath(pathname, 0);
                
                int argc = 0;
                while (guest_argv && guest_argv[argc]) argc++;
                
                std::vector<std::string> new_argv;
                for (int i = 0; i < argc; ++i) {
                    new_argv.push_back(guest_argv[i]);
                }

                std::vector<std::string> envVars;
                for (int i = 0; guest_envp && guest_envp[i]; ++i) {
                    envVars.push_back(guest_envp[i]);
                }
                
                std::string rootfs = "";
                if (SeccompTrap::getInstance().rootfsDfd_ >= 0) {
                    rootfs = vfs.resolvePath("/", 0);
                }

                LOGI("Guest Syscall: execve(%s) intercepted via ElfLoader", resolvedPath.c_str());
                if (!ElfLoader::execute(resolvedPath, new_argv, envVars, rootfs)) {
                    ret = -ENOEXEC;
                    LOGE("Guest Syscall: ElfLoader failed for %s", resolvedPath.c_str());
                } else {
                    // ElfLoader::execute will jump to entry and NEVER RETURN here!
                }
            } else {
                ret = -EFAULT;
            }
            break;
        }
#endif
#ifdef __NR_readlinkat
        case __NR_readlinkat: {
            const char* pathname = reinterpret_cast<const char*>(arg1);
            char* buf = reinterpret_cast<char*>(arg2);
            size_t bufsiz = static_cast<size_t>(arg3);

            if (pathname) {
                std::string resolved = vfs.resolvePath(pathname, 0);
                int targetDfd = (resolved.empty() || resolved[0] == '/') ? AT_FDCWD : dfd;
                ret = syscall(__NR_readlinkat, targetDfd, resolved.c_str(), buf, bufsiz);
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
            const char* src = reinterpret_cast<const char*>(arg0);
            const char* tgt = reinterpret_cast<const char*>(arg1);
            const char* fstype = reinterpret_cast<const char*>(arg2);
            unsigned long mflags = static_cast<unsigned long>(arg3);
            const void* data = reinterpret_cast<const void*>(uctx->uc_mcontext.regs[4]);
            ret = UserKernel::getInstance().sysMount(src, tgt, fstype, mflags, data);
            break;
        }
#endif
#ifdef __NR_umount2
        case __NR_umount2: {
            const char* tgt = reinterpret_cast<const char*>(arg0);
            int flags = static_cast<int>(arg1);
            ret = UserKernel::getInstance().sysUmount(tgt, flags);
            break;
        }
#endif
#ifdef __NR_pivot_root
        case __NR_pivot_root: {
            const char* nroot = reinterpret_cast<const char*>(arg0);
            const char* pold = reinterpret_cast<const char*>(arg1);
            ret = UserKernel::getInstance().sysPivotRoot(nroot, pold);
            break;
        }
#endif
#ifdef __NR_chroot
        case __NR_chroot: {
            const char* path = reinterpret_cast<const char*>(arg0);
            ret = UserKernel::getInstance().sysChroot(path);
            break;
        }
#endif
#ifdef __NR_mknodat
        case __NR_mknodat: {
            int dirFd = static_cast<int>(arg0);
            const char* pathname = reinterpret_cast<const char*>(arg1);
            mode_t mode = static_cast<mode_t>(arg2);
            dev_t dev = static_cast<dev_t>(arg3);
            ret = UserKernel::getInstance().sysMknodat(dirFd, pathname, mode, dev);
            break;
        }
#endif
#ifdef __NR_mknod
        case __NR_mknod: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setuid
        case __NR_setuid: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setgid
        case __NR_setgid: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setuid32
        case __NR_setuid32: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setgid32
        case __NR_setgid32: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setreuid
        case __NR_setreuid: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setregid
        case __NR_setregid: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setresuid
        case __NR_setresuid: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setresgid
        case __NR_setresgid: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setgroups
        case __NR_setgroups: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_capset
        case __NR_capset: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_sethostname
        case __NR_sethostname: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_setdomainname
        case __NR_setdomainname: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_reboot
        case __NR_reboot: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_swapon
        case __NR_swapon: {
            ret = 0;
            break;
        }
#endif
#ifdef __NR_swapoff
        case __NR_swapoff: {
            ret = 0;
            break;
        }
#endif
        default:
            ret = -ENOSYS;
            break;
    }

    uctx->uc_mcontext.regs[0] = static_cast<uint64_t>(ret);
    uctx->uc_mcontext.pc += 4;

#elif defined(__arm__)
    uint32_t syscallNr = uctx->uc_mcontext.arm_r7;
    uint32_t arg1 = uctx->uc_mcontext.arm_r1;
    uint32_t arg2 = uctx->uc_mcontext.arm_r2;
    uint32_t arg3 = uctx->uc_mcontext.arm_r3;
    int32_t ret = 0;

    if (syscallNr == __NR_openat && arg1 != 0) {
        const char* pathname = reinterpret_cast<const char*>(arg1);
        std::string resolved = vfs.resolvePath(pathname, arg2);
        ret = syscall(__NR_openat, SECCOMP_BYPASS_MAGIC, resolved.c_str(), arg2, arg3);
        if (ret < 0) ret = -errno;
    } else {
        ret = 0;
    }

    uctx->uc_mcontext.arm_r0 = static_cast<uint32_t>(ret);
    uctx->uc_mcontext.arm_pc += 4;
#endif
}

} // namespace vmgo
