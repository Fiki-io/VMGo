#pragma once
#include <string>
#include <vector>

namespace vmgo {

class ElfLoader {
public:
    static bool execute(const std::string& binaryPath,
                        const std::vector<std::string>& args,
                        const std::vector<std::string>& envVars,
                        const std::string& sandboxRoot);
};

} // namespace vmgo
