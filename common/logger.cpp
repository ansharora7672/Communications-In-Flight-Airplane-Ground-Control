#include "logger.h"

#include <ctime>
#include <iomanip>
#include <sstream>

namespace {

std::string makeTimestampPrefix() {
    const std::time_t now = std::time(nullptr);
    const std::tm* utcNow = std::gmtime(&now);

    std::ostringstream out;
    out << std::put_time(utcNow, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string makeCommsLogName(const std::string& role, const std::string& prefix) {
    return prefix + "_" + role + "_comms.log";
}

std::string makeBlackBoxLogName(const std::string& prefix) {
    return prefix + "_blackbox.log";
}

} // namespace

Logger::Logger(const std::string& role) {
    const std::string prefix = makeTimestampPrefix();
    commsLog.open(makeCommsLogName(role, prefix), std::ios::out);
    if (role == "groundctrl") {
        blackBoxLog.open(makeBlackBoxLogName(prefix), std::ios::out);
    }
}

void Logger::logPacket(const std::string& direction, const PacketHeader& hdr) {
    std::lock_guard<std::mutex> lock(mutex);
    commsLog << "[" << timestampUtc() << "] "
             << "[" << direction << "] "
             << "[" << packetTypeToString(hdr.packet_type) << "] "
             << "Aircraft=" << extractAircraftId(hdr.aircraft_id) << ' '
             << "Seq=" << std::setw(4) << std::setfill('0') << hdr.sequence_number << ' '
             << "PayloadSize=" << hdr.payload_size << '\n';
    commsLog.flush();
}

void Logger::logFault(const std::string& cause, const std::string& state, std::uint32_t seq) {
    std::lock_guard<std::mutex> lock(mutex);
    std::ostringstream line;
    line << "[" << timestampUtc() << "] "
         << "[FAULT] "
         << "Cause=" << cause << ' '
         << "State=" << state << ' '
         << "Seq=" << std::setw(4) << std::setfill('0') << seq;

    commsLog << line.str() << '\n';
    commsLog.flush();

    if (blackBoxLog.is_open()) {
        blackBoxLog << line.str() << '\n';
        blackBoxLog.flush();
    }
}

void Logger::logInfo(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex);
    commsLog << "[" << timestampUtc() << "] "
             << "[INFO] "
             << message << '\n';
    commsLog.flush();
}

std::string Logger::timestampUtc() const {
    const std::time_t now = std::time(nullptr);
    const std::tm* utcNow = std::gmtime(&now);

    std::ostringstream out;
    out << std::put_time(utcNow, "%Y-%m-%d %H:%M:%S UTC");
    return out.str();
}
