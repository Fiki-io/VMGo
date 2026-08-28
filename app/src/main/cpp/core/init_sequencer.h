#ifndef INIT_SEQUENCER_H
#define INIT_SEQUENCER_H

#include "../include/vm_types.h"
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>

namespace vmgo {

class InitSequencer {
public:
    static InitSequencer& getInstance();

    using LogCallback = std::function<void(const std::string& line)>;

    bool startSequence(const VmConfiguration& config, LogCallback onLog = nullptr);
    void stop();
    bool isRunning() const { return running_; }

private:
    InitSequencer() = default;
    ~InitSequencer() { stop(); }

    bool startDaemon(const std::string& binaryPath, const std::vector<std::string>& args, const std::string& rootfs, const std::string& name);

    std::atomic<bool> running_{false};
    std::vector<pid_t> daemonPids_;
};

} // namespace vmgo

#endif // INIT_SEQUENCER_H
