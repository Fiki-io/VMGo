#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <string>
#include <vector>
#include <cstdint>

namespace vmgo {

class ElfLoader {
public:
    /**
     * User-space ELF Loader (In-Memory Loader)
     * Loads and executes an ELF binary (and its interpreter if dynamic) directly in memory,
     * setting up the stack, arguments, environment, and auxiliary vector.
     * Bypasses Android 10+ W^X SELinux 'execve' restrictions on app private directories.
     */
    static bool execute(
        const std::string& binaryPath,
        const std::vector<std::string>& args,
        const std::vector<std::string>& envVars,
        const std::string& sandboxRoot
    );
};

} // namespace vmgo

#endif // ELF_LOADER_H
