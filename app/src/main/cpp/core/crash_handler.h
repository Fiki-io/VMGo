#ifndef CRASH_HANDLER_H
#define CRASH_HANDLER_H

#include "../include/vm_types.h"
#include <csignal>
#include <string>

namespace vmgo {

class CrashHandler {
public:
    static void install();
    static void handleSignal(int sig, siginfo_t* info, void* context);
};

} // namespace vmgo

#endif // CRASH_HANDLER_H
