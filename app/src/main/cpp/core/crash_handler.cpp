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
        case SIGSEGV: return "SIGSEGV (Segmentation Fault - Invalid Memory Address)";
        case SIGBUS:  return "SIGBUS (Bus Error - Alignment or Memory Fault)";
        case SIGABRT: return "SIGABRT (Abort Signal - Runtime Assertion Failed)";
        case SIGILL:  return "SIGILL (Illegal Instruction)";
        case SIGFPE:  return "SIGFPE (Arithmetic Exception)";
        case SIGSYS:  return "SIGSYS (Seccomp Syscall Trap)";
        default:      return "UNKNOWN_SIGNAL";
    }
}

void CrashHandler::install() {
    // Setup dedicated alternate stack for signal delivery
    static uint8_t altStackMem[SIGSTKSZ * 8];
    stack_t ss{};
    ss.ss_sp = altStackMem;
    ss.ss_size = sizeof(altStackMem);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = CrashHandler::handleSignal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);

    LOGI("CrashHandler: Diagnostic crash interceptors installed on alternate stack");
}

void CrashHandler::handleSignal(int sig, siginfo_t* info, void* context) {
    char msg[1024];
    snprintf(msg, sizeof(msg),
             "\n[CRASH] ==================== GUEST PROCESS CRASH REPORT ====================\n"
             "[CRASH] REASON: %s (Signal %d, Code %d)\n"
             "[CRASH] FAULT ADDRESS: %p\n",
             getSignalName(sig), sig, info ? info->si_code : 0, info ? info->si_addr : nullptr);
    write(STDERR_FILENO, msg, strlen(msg));

    auto* uctx = reinterpret_cast<ucontext_t*>(context);
    if (uctx) {
#if defined(__aarch64__)
        uint64_t pc = uctx->uc_mcontext.pc;
        uint64_t sp = uctx->uc_mcontext.sp;
        uint64_t lr = uctx->uc_mcontext.regs[30];

        snprintf(msg, sizeof(msg),
                 "[CRASH] CPU STATE (ARM64):\n"
                 "[CRASH]   PC: 0x%016llx  SP: 0x%016llx  LR: 0x%016llx\n"
                 "[CRASH]   x0: 0x%016llx  x1: 0x%016llx  x2: 0x%016llx  x3: 0x%016llx\n"
                 "[CRASH]   x4: 0x%016llx  x5: 0x%016llx  x6: 0x%016llx  x7: 0x%016llx\n"
                 "[CRASH]   x8 (Syscall): 0x%016llx  x16: 0x%016llx  x29 (FP): 0x%016llx\n",
                 (unsigned long long)pc, (unsigned long long)sp, (unsigned long long)lr,
                 (unsigned long long)uctx->uc_mcontext.regs[0], (unsigned long long)uctx->uc_mcontext.regs[1],
                 (unsigned long long)uctx->uc_mcontext.regs[2], (unsigned long long)uctx->uc_mcontext.regs[3],
                 (unsigned long long)uctx->uc_mcontext.regs[4], (unsigned long long)uctx->uc_mcontext.regs[5],
                 (unsigned long long)uctx->uc_mcontext.regs[6], (unsigned long long)uctx->uc_mcontext.regs[7],
                 (unsigned long long)uctx->uc_mcontext.regs[8], (unsigned long long)uctx->uc_mcontext.regs[16],
                 (unsigned long long)uctx->uc_mcontext.regs[29]);
        write(STDERR_FILENO, msg, strlen(msg));
#endif
    }

    snprintf(msg, sizeof(msg), "[CRASH] ==================================================================\n\n");
    write(STDERR_FILENO, msg, strlen(msg));

    // Reset and trigger default action to cleanly exit
    signal(sig, SIG_DFL);
    raise(sig);
}

} // namespace vmgo
