#include "qemu_pipe_server.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace vmgo {

QemuPipeServer& QemuPipeServer::getInstance() {
    static QemuPipeServer instance;
    return instance;
}

bool QemuPipeServer::start(const std::string& socketPath) {
    if (isRunning_) {
        return true;
    }

    socketPath_ = socketPath;
    unlink(socketPath_.c_str());

    serverFd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        LOGE("Failed to create UNIX domain socket for QEMU Pipe: %s", strerror(errno));
        return false;
    }

    struct sockaddr_un addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(serverFd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        LOGE("Failed to bind QEMU pipe socket to %s: %s", socketPath_.c_str(), strerror(errno));
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    if (listen(serverFd_, 32) != 0) {
        LOGE("Failed to listen on QEMU pipe socket: %s", strerror(errno));
        close(serverFd_);
        serverFd_ = -1;
        return false;
    }

    isRunning_ = true;
    serverThread_ = std::make_unique<std::thread>(&QemuPipeServer::serverLoop, this);
    LOGI("QEMU Pipe Server started on %s", socketPath_.c_str());
    return true;
}

void QemuPipeServer::stop() {
    if (!isRunning_) return;

    isRunning_ = false;
    if (serverFd_ >= 0) {
        shutdown(serverFd_, SHUT_RDWR);
        close(serverFd_);
        serverFd_ = -1;
    }

    if (serverThread_ && serverThread_->joinable()) {
        serverThread_->join();
    }

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        for (int fd : sensorClients_) close(fd);
        for (int fd : gpsClients_) close(fd);
        for (int fd : hwControlClients_) close(fd);
        sensorClients_.clear();
        gpsClients_.clear();
        hwControlClients_.clear();
    }

    unlink(socketPath_.c_str());
    LOGI("QEMU Pipe Server stopped");
}

void QemuPipeServer::serverLoop() {
    while (isRunning_) {
        struct sockaddr_un clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(serverFd_, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);
        if (clientFd < 0) {
            if (!isRunning_) break;
            LOGW("QEMU pipe accept error: %s", strerror(errno));
            continue;
        }

        // Spawn a thread or handle connection
        std::thread([this, clientFd]() {
            this->handleClientConnection(clientFd);
        }).detach();
    }
}

void QemuPipeServer::handleClientConnection(int clientFd) {
    // QEMU Pipe Handshake: Read service name terminated by '\0'
    char buffer[256];
    ssize_t n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        close(clientFd);
        return;
    }
    buffer[n] = '\0';
    std::string serviceName(buffer);

    // Response header: "ok" or error code
    const char* okResp = "ok";
    send(clientFd, okResp, strlen(okResp) + 1, 0);

    LOGI("QEMU Pipe Channel Connected: %s (fd=%d)", serviceName.c_str(), clientFd);
    HalChannelType type = identifyService(serviceName);

    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        switch (type) {
            case HalChannelType::SENSORS:
                sensorClients_.push_back(clientFd);
                break;
            case HalChannelType::GPS:
                gpsClients_.push_back(clientFd);
                break;
            case HalChannelType::HW_CONTROL:
                hwControlClients_.push_back(clientFd);
                break;
            default:
                break;
        }
    }

    // Keep connection alive and read guest commands
    while (isRunning_) {
        ssize_t bytes = recv(clientFd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) break;

        // Process guest HAL request
        if (type == HalChannelType::AUDIO && audioCb_) {
            audioCb_(reinterpret_cast<const uint8_t*>(buffer), static_cast<size_t>(bytes));
        }
    }

    // Client disconnected
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        sensorClients_.erase(std::remove(sensorClients_.begin(), sensorClients_.end(), clientFd), sensorClients_.end());
        gpsClients_.erase(std::remove(gpsClients_.begin(), gpsClients_.end(), clientFd), gpsClients_.end());
        hwControlClients_.erase(std::remove(hwControlClients_.begin(), hwControlClients_.end(), clientFd), hwControlClients_.end());
    }
    close(clientFd);
}

HalChannelType QemuPipeServer::identifyService(const std::string& serviceName) {
    if (serviceName.find("sensors") != std::string::npos) return HalChannelType::SENSORS;
    if (serviceName.find("gps") != std::string::npos) return HalChannelType::GPS;
    if (serviceName.find("camera") != std::string::npos) return HalChannelType::CAMERA;
    if (serviceName.find("audio") != std::string::npos) return HalChannelType::AUDIO;
    if (serviceName.find("hw-control") != std::string::npos) return HalChannelType::HW_CONTROL;
    if (serviceName.find("wifi") != std::string::npos) return HalChannelType::WIFI;
    if (serviceName.find("boot-properties") != std::string::npos) return HalChannelType::BOOT_PROPERTIES;
    if (serviceName.find("adb") != std::string::npos) return HalChannelType::ADB;
    return HalChannelType::UNKNOWN;
}

void QemuPipeServer::sendSensorUpdate(const std::string& sensorName, float v0, float v1, float v2) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    if (sensorClients_.empty()) return;

    char msg[128];
    int len = snprintf(msg, sizeof(msg), "%s:%f:%f:%f\n", sensorName.c_str(), v0, v1, v2);
    for (int fd : sensorClients_) {
        send(fd, msg, len, MSG_NOSIGNAL);
    }
}

void QemuPipeServer::sendGpsLocation(double lat, double lon, float alt, float speed, float bearing, uint64_t /* timestamp */) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    if (gpsClients_.empty()) return;

    char msg[256];
    int len = snprintf(msg, sizeof(msg), "gps:%f:%f:%f:%f:%f\n", lat, lon, alt, speed, bearing);
    for (int fd : gpsClients_) {
        send(fd, msg, len, MSG_NOSIGNAL);
    }
}

void QemuPipeServer::sendNmeaSentence(const std::string& nmea) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    if (gpsClients_.empty()) return;

    for (int fd : gpsClients_) {
        send(fd, nmea.c_str(), nmea.length(), MSG_NOSIGNAL);
    }
}

void QemuPipeServer::sendBatteryStatus(int level, bool isCharging, int temperature) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    if (hwControlClients_.empty()) return;

    char msg[128];
    int len = snprintf(msg, sizeof(msg), "power:battery:%d:%d:%d\n", level, isCharging ? 1 : 0, temperature);
    for (int fd : hwControlClients_) {
        send(fd, msg, len, MSG_NOSIGNAL);
    }
}

void QemuPipeServer::setAudioCallback(AudioOutputCallback cb) {
    audioCb_ = cb;
}

} // namespace vmgo
