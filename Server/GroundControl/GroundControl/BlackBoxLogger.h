#pragma once
#include <fstream>
#include <string>
#include <mutex>

class BlackBoxLogger {
public:
    BlackBoxLogger(const std::string& filePath)
        : stream_(filePath.c_str(), std::ios::out | std::ios::app) {}

    void logFault(const std::string& cause) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stream_.is_open()) {
            stream_ << currentUtcTimestamp() << " FAULT: " << cause << "\n";
            stream_.flush();
        }
    }

private:
    static std::string currentUtcTimestamp() {
        std::time_t now = std::time(nullptr);
        char buf[32] = {};
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
        return std::string(buf);
    }
    std::ofstream stream_;
    std::mutex mutex_;
};
