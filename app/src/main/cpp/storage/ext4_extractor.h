#ifndef EXT4_EXTRACTOR_H
#define EXT4_EXTRACTOR_H

#include <string>
#include <cstdint>
#include <functional>

namespace vmgo {

class Ext4Extractor {
public:
    using ProgressCallback = std::function<void(int percent, const std::string& currentFile)>;

    static bool extractExt4Image(
        const std::string& imagePath,
        const std::string& targetDir,
        ProgressCallback onProgress = nullptr
    );

    static bool isExt4Image(const std::string& imagePath);
};

} // namespace vmgo

#endif // EXT4_EXTRACTOR_H
