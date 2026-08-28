#include "crash_handler.h"
#include <unistd.h>
#include <fcntl.h>
#include <ucontext.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace vmgo {

static const char* getSignalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (Segmentation Fault - Invalid Memory Access)";
        case SIGBUS:  return "SIGBUS (Bus Error - Invalid Alignment or Mmap Access)";
        case SIGABRT: return "SIGABRT (Abort Signal - Runtime Assertion/Abort)";
        case SIGILL:  return "SIGILL (Illegal Instruction)";
        case SIGFPE:  return "SIGFPE (Floating Point / Arithmetic Exception)";
        case SIGSYS:  return "SIGSYS (Bad / Untrapped Syscall)";
        default:      return "UNKNOWN_SIGNAL";
    }
}

void CrashHandler::install() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = CrashHandler::handleSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);

    LOGI("CrashHandler: Diagnostic crash interceptors installed");
}

void CrashHandler::handleSignal(int sig, siginfo_t* info, void* context) {
    const char* sigName = getSignalName(sig);
    void* faultAddr = info ? info->si_addr : nullptr;
    int siCode = info ? info->si_code : 0;

    LOGE("==================== CRASH DIAGNOSTIC REPORT ====================");
    LOGE("CRASH OCCURRED: %s (Signal %d, Code %d)", sigName, sig, siCode);
    LOGE("Faulting Memory Address: %p", faultAddr);

    auto* uctx = reinterpret_cast<ucontext_t*>(context);
    if (uctx) {
#if defined(__aarch64__)
        uint64_t pc = uctx->uc_mcontext.pc;
        uint64_t sp = uctx->uc_mcontext.sp;
        uint64_t lr = uctx->uc_mcontext.regs[30];

        LOGE("CPU State (ARM64):");
        LOGE("  PC: 0x%016llx  SP: 0x%016llx  LR: 0x%016llx  PSTATE: 0x%08llx",
             (unsigned long long)pc, (unsigned long long)sp, (unsigned long long)lr,
             (unsigned long long)uctx->uc_mcontext.pstate);

        LOGE("  x0:  0x%016llx  x1:  0x%016llx  x2:  0x%016llx  x3:  0x%016llx",
             (unsigned long long)uctx->uc_mcontext.regs[0], (unsigned long long)uctx->uc_mcontext.regs[1],
             (unsigned long long)uctx->uc_mcontext.regs[2], (unsigned long long)uctx->uc_mcontext.regs[3]);
        LOGE("  x4:  0x%016llx  x5:  0x%016llx  x6:  0x%016llx  x7:  0x%016llx",
             (unsigned long long)uctx->uc_mcontext.regs[4], (unsigned long long)uctx->uc_mcontext.regs[5],
             (unsigned long long)uctx->uc_mcontext.regs[6], (unsigned long long)uctx->uc_mcontext.regs[7]);
        LOGE("  x8:  0x%016llx  x9:  0x%016llx  x10: 0x%016llx  x11: 0x%016llx",
             (unsigned long long)uctx->uc_mcontext.regs[8], (unsigned long long)uctx->uc_mcontext.regs[9],
             (unsigned long long)uctx->uc_mcontext.regs[10], (unsigned long long)uctx->uc_mcontext.regs[11]);
        LOGE("  x16: 0x%016llx  x17: 0x%016llx  x29: 0x%016llx  x30: 0x%016llx",
             (unsigned long long)uctx->uc_mcontext.regs[16], (unsigned long long)uctx->uc_mcontext.regs[17],
             (unsigned long long)uctx->uc_mcontext.regs[29], (unsigned long long)uctx->uc_mcontext.regs[30]);

#elif defined(__arm__)
        LOGE("CPU State (ARM32): PC=0x%08lx SP=0x%08lx LR=0x%08lx",
             (unsigned long)uctx->uc_mcontext.arm_pc,
             (unsigned long)uctx->uc_mcontext.arm_sp,
             (unsigned long)uctx->uc_mcontext.arm_lr);
#endif
    }

    // Inspect /proc/self/maps to identify the crashed library
    FILE* fp = fopen("/proc/self/maps", "r");
    if (fp) {
        LOGE("Memory Maps at Crash Time (matching PC):");
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            // Trim newline
            line[strcspn(line, "\r\n")] = 0;
            if (strstr(line, "r-xp") || strstr(line, "linker") || strstr(line, "app_process") || strstr(line, "libart")) {
                LOGE("  %s", line);
            }
        }
        fclose(fp);
    }

    LOGE("==================== END CRASH REPORT ====================");

    // Reset signal handler to default and re-raise to finish cleanup
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace vmgo
