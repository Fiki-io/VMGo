#ifndef SECCOMP_TRAP_H
#define SECCOMP_TRAP_H

#include "../include/vm_types.h"
#include <signal.h>
#include <ucontext.h>

namespace vmgo {

class SeccompTrap {
public:
    static SeccompTrap& getInstance();

    bool installFilter();
    void uninstall();

    static void sigsysHandler(int sig, siginfo_t* info, void* context);

private:
    SeccompTrap() = default;
    ~SeccompTrap() = default;

    bool installed_ = false;
    struct sigaction oldSigsysAction_{};
};

} // namespace vmgo

#endif // SECCOMP_TRAP_H
