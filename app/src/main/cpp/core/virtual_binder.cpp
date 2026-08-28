#include "virtual_binder.h"
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cerrno>

namespace vmgo {

#define BINDER_WRITE_READ_CMD _IOWR('b', 1, struct VirtualBinderWriteRead)
#define BINDER_SET_IDLE_TIMEOUT _IOW('b', 3, int64_t)
#define BINDER_SET_MAX_THREADS_CMD _IOW('b', 5, uint32_t)
#define BINDER_SET_IDLE_PRIORITY _IOW('b', 6, int32_t)
#define BINDER_SET_CONTEXT_MGR_CMD _IOW('b', 7, int32_t)
#define BINDER_THREAD_EXIT_CMD _IOW('b', 8, int32_t)
#define BINDER_VERSION_CMD _IOWR('b', 9, struct VirtualBinderVersion)

VirtualBinder& VirtualBinder::getInstance() {
    static VirtualBinder instance;
    return instance;
}

void VirtualBinder::initialize(const std::string& sandboxDevPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    sandboxDevPath_ = sandboxDevPath;
    initialized_ = true;
    LOGI("VirtualBinder: Initialized for sandbox dev path: %s", sandboxDevPath.c_str());
}

void VirtualBinder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : binderMmapBuffers_) {
        if (pair.second && pair.second != MAP_FAILED) {
            size_t sz = binderMmapSize_[pair.first];
            if (sz > 0) {
                munmap(pair.second, sz);
            }
        }
    }
    binderMmapBuffers_.clear();
    binderMmapSize_.clear();
    openBinderFds_.clear();
    contextMgrFd_ = -1;
    initialized_ = false;
    LOGI("VirtualBinder: Reset");
}

bool VirtualBinder::isBinderFd(int fd) const {
    if (fd < 0) return false;
    return openBinderFds_.find(fd) != openBinderFds_.end();
}

void VirtualBinder::registerBinderFd(int fd, const std::string& devName) {
    std::lock_guard<std::mutex> lock(mutex_);
    openBinderFds_[fd] = devName;
    LOGI("VirtualBinder: Registered fd %d as node %s", fd, devName.c_str());
}

void VirtualBinder::unregisterBinderFd(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = openBinderFds_.find(fd);
    if (it != openBinderFds_.end()) {
        openBinderFds_.erase(it);
    }
    auto mIt = binderMmapBuffers_.find(fd);
    if (mIt != binderMmapBuffers_.end()) {
        if (mIt->second && mIt->second != MAP_FAILED) {
            munmap(mIt->second, binderMmapSize_[fd]);
        }
        binderMmapBuffers_.erase(mIt);
        binderMmapSize_.erase(fd);
    }
    if (contextMgrFd_ == fd) {
        contextMgrFd_ = -1;
    }
}

int VirtualBinder::handleIoctl(int fd, unsigned long request, void* arg) {
    if (!arg && request != BINDER_SET_CONTEXT_MGR_CMD) {
        return -EINVAL;
    }

    switch (request) {
        case BINDER_VERSION_CMD: {
            auto* ver = reinterpret_cast<VirtualBinderVersion*>(arg);
            if (ver) {
                ver->protocol_version = BINDER_CURRENT_PROTOCOL_VERSION;
            }
            return 0;
        }

        case BINDER_SET_MAX_THREADS_CMD: {
            uint32_t maxTh = *reinterpret_cast<uint32_t*>(arg);
            return handleSetMaxThreads(fd, maxTh);
        }

        case BINDER_SET_CONTEXT_MGR_CMD: {
            return handleSetContextMgr(fd);
        }

        case BINDER_THREAD_EXIT_CMD: {
            return 0;
        }

        case BINDER_WRITE_READ_CMD: {
            auto* bwr = reinterpret_cast<VirtualBinderWriteRead*>(arg);
            return handleWriteRead(fd, bwr);
        }

        default:
            LOGI("VirtualBinder: Unhandled ioctl request 0x%lx on fd %d", request, fd);
            return 0; // Return success to keep bionic / framework flow continuing
    }
}

int VirtualBinder::handleSetContextMgr(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    contextMgrFd_ = fd;
    LOGI("VirtualBinder: Context Manager (servicemanager) registered on fd %d", fd);
    return 0;
}

int VirtualBinder::handleSetMaxThreads(int /* fd */, uint32_t maxThreads) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxThreads_ = maxThreads;
    LOGI("VirtualBinder: Max threads configured: %u", maxThreads);
    return 0;
}

int VirtualBinder::handleWriteRead(int /* fd */, VirtualBinderWriteRead* bwr) {
    if (!bwr) return -EINVAL;

    // Process write commands if present
    if (bwr->write_size > 0 && bwr->write_buffer != 0) {
        bwr->write_consumed = bwr->write_size;
    }

    // Process read requests
    if (bwr->read_size > 0 && bwr->read_buffer != 0) {
        uint8_t* readPtr = reinterpret_cast<uint8_t*>(bwr->read_buffer);
        size_t available = bwr->read_size;
        size_t consumed = 0;

        // Return BR_NOOP and BR_TRANSACTION_COMPLETE
        if (available >= sizeof(uint32_t) * 2) {
            *reinterpret_cast<uint32_t*>(readPtr + consumed) = BR_NOOP;
            consumed += sizeof(uint32_t);

            *reinterpret_cast<uint32_t*>(readPtr + consumed) = BR_TRANSACTION_COMPLETE;
            consumed += sizeof(uint32_t);
        } else if (available >= sizeof(uint32_t)) {
            *reinterpret_cast<uint32_t*>(readPtr + consumed) = BR_NOOP;
            consumed += sizeof(uint32_t);
        }

        bwr->read_consumed = consumed;
    }

    return 0;
}

void* VirtualBinder::handleMmap(int fd, void* addr, size_t length, int prot, int flags, off_t offset) {
    std::lock_guard<std::mutex> lock(mutex_);

    void* mem = mmap(addr, length, prot, flags | MAP_ANONYMOUS, -1, offset);
    if (mem != MAP_FAILED) {
        binderMmapBuffers_[fd] = mem;
        binderMmapSize_[fd] = length;
        LOGI("VirtualBinder: Allocated %zu bytes mmap buffer for fd %d at %p", length, fd, mem);
    }
    return mem;
}

} // namespace vmgo
