#ifndef VIRTUAL_BINDER_H
#define VIRTUAL_BINDER_H

#include "../include/vm_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <sys/types.h>
#include <linux/types.h>
#include <linux/ioctl.h>

namespace vmgo {

#define BINDER_CURRENT_PROTOCOL_VERSION 8

struct VirtualBinderVersion {
    int32_t protocol_version;
};

struct VirtualBinderWriteRead {
    uint64_t write_size;
    uint64_t write_consumed;
    uintptr_t write_buffer;
    uint64_t read_size;
    uint64_t read_consumed;
    uintptr_t read_buffer;
};

// Common Binder Command / Return Protocol Codes
enum BinderDriverCommandProtocol {
    BC_NOOP = 0x7300,
    BC_TRANSACTION = 0x40406300,
    BC_REPLY = 0x40406301,
    BC_ACQUIRE_RESULT = 0x40046302,
    BC_FREE_BUFFER = 0x40086303,
    BC_INCREFS = 0x40046304,
    BC_ACQUIRE = 0x40046305,
    BC_RELEASE = 0x40046306,
    BC_DECREFS = 0x40046307,
    BC_INCREFS_DONE = 0x40106308,
    BC_ACQUIRE_DONE = 0x40106309,
    BC_ATTEMPT_ACQUIRE = 0x4018630a,
    BC_REGISTER_LOOPER = 0x730b,
    BC_ENTER_LOOPER = 0x730c,
    BC_EXIT_LOOPER = 0x730d,
    BC_REQUEST_DEATH_NOTIFICATION = 0x4014630e,
    BC_CLEAR_DEATH_NOTIFICATION = 0x4014630f,
    BC_DEAD_BINDER_DONE = 0x40086310
};

enum BinderDriverReturnProtocol {
    BR_ERROR = 0x80047200,
    BR_OK = 0x7201,
    BR_TRANSACTION_SEC_CTX = 0x80487202,
    BR_TRANSACTION = 0x80407202,
    BR_REPLY = 0x80407203,
    BR_ACQUIRE_RESULT = 0x80047204,
    BR_DEAD_REPLY = 0x7205,
    BR_TRANSACTION_COMPLETE = 0x7206,
    BR_INCREFS = 0x80107207,
    BR_ACQUIRE = 0x80107208,
    BR_RELEASE = 0x80107209,
    BR_DECREFS = 0x8010720a,
    BR_NOOP = 0x720c,
    BR_SPAWN_LOOPER = 0x720d,
    BR_FINISHED = 0x720e,
    BR_DEAD_BINDER = 0x8008720f,
    BR_CLEAR_DEATH_NOTIFICATION_DONE = 0x80087210,
    BR_FAILED_REPLY = 0x7211
};

class VirtualBinder {
public:
    static VirtualBinder& getInstance();

    void initialize(const std::string& sandboxDevPath);
    void reset();

    // Check if an opened fd belongs to a virtual binder node
    bool isBinderFd(int fd) const;
    void registerBinderFd(int fd, const std::string& devName);
    void unregisterBinderFd(int fd);

    // Handle ioctl on virtual binder
    int handleIoctl(int fd, unsigned long request, void* arg);

    // Handle mmap on virtual binder (buffer allocation)
    void* handleMmap(int fd, void* addr, size_t length, int prot, int flags, off_t offset);

private:
    VirtualBinder() = default;
    ~VirtualBinder() = default;

    int handleWriteRead(int fd, VirtualBinderWriteRead* bwr);
    int handleSetContextMgr(int fd);
    int handleSetMaxThreads(int fd, uint32_t maxThreads);

    std::mutex mutex_;
    bool initialized_ = false;
    std::string sandboxDevPath_;
    int contextMgrFd_ = -1;
    uint32_t maxThreads_ = 16;

    std::unordered_map<int, std::string> openBinderFds_;
    std::unordered_map<int, void*> binderMmapBuffers_;
    std::unordered_map<int, size_t> binderMmapSize_;
};

} // namespace vmgo

#endif // VIRTUAL_BINDER_H
