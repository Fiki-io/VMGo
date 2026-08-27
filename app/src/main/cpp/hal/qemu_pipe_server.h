#ifndef QEMU_PIPE_SERVER_H
#define QEMU_PIPE_SERVER_H

#include "../include/vm_types.h"
#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <functional>

namespace vmgo {

using SensorDataCallback = std::function<void(const std::string& sensorName, float v0, float v1, float v2)>;
using GpsDataCallback = std::function<void(double lat, double lon, float alt, float speed)>;
using AudioOutputCallback = std::function<void(const uint8_t* pcm, size_t size)>;

class QemuPipeServer {
public:
    static QemuPipeServer& getInstance();

    bool start(const std::string& socketPath);
    void stop();

    // Data injection from Host (Kotlin) into Guest OS
    void sendSensorUpdate(const std::string& sensorName, float v0, float v1, float v2);
    void sendGpsLocation(double lat, double lon, float alt, float speed, float bearing, uint64_t timestamp);
    void sendNmeaSentence(const std::string& nmea);
    void sendBatteryStatus(int level, bool isCharging, int temperature);

    void setAudioCallback(AudioOutputCallback cb);

private:
    QemuPipeServer() = default;
    ~QemuPipeServer() { stop(); }

    void serverLoop();
    void handleClientConnection(int clientFd);
    HalChannelType identifyService(const std::string& serviceName);

    std::string socketPath_;
    int serverFd_ = -1;
    std::atomic<bool> isRunning_{false};
    std::unique_ptr<std::thread> serverThread_;

    std::mutex clientsMutex_;
    std::vector<int> sensorClients_;
    std::vector<int> gpsClients_;
    std::vector<int> hwControlClients_;

    AudioOutputCallback audioCb_;
};

} // namespace vmgo

#endif // QEMU_PIPE_SERVER_H
