#pragma once
#include <string>
#include <vector>

namespace vmgo {

class ElfLoader {
public:
    static bool execute(const std::string& sandboxRoot,
                        const std::string& binaryPath,
                        const std::string& interpPath,
                        const std::vector<std::string>& args,
                        const std::vector<std::string>& envVars);
};

} // namespace vmgo
