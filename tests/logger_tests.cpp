#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "logger.h"
#include "test_helpers.h"

TEST(LoggerTest, GroundRoleCreatesCommsAndBlackboxLogs) {
    const std::filesystem::path logsRoot = makeTempDirectory("logger_ground");

    {
        Logger logger("groundctrl", logsRoot);
        logger.logInfo("Ground logger initialized");
    }

    const std::filesystem::path commsLog = onlyFileIn(logsRoot / "groundctrl_comms");
    const std::filesystem::path blackboxLog = onlyFileIn(logsRoot / "blackbox");
    EXPECT_EQ(commsLog.extension(), ".log");
    EXPECT_EQ(blackboxLog.extension(), ".log");
    EXPECT_NE(commsLog.filename().string().find("_groundctrl_comms.log"), std::string::npos);
    EXPECT_NE(blackboxLog.filename().string().find("_blackbox.log"), std::string::npos);
}

TEST(LoggerTest, AircraftRoleCreatesOnlyCommsLog) {
    const std::filesystem::path logsRoot = makeTempDirectory("logger_aircraft");

    {
        Logger logger("aircraft", logsRoot);
        logger.logInfo("Aircraft logger initialized");
    }

    const std::filesystem::path commsLog = onlyFileIn(logsRoot / "aircraft_comms");
    EXPECT_TRUE(std::filesystem::exists(commsLog));
    EXPECT_FALSE(std::filesystem::exists(logsRoot / "blackbox"));
}

TEST(LoggerTest, PacketLogIncludesExpectedFields) {
    const std::filesystem::path logsRoot = makeTempDirectory("logger_packet");

    {
        Logger logger("groundctrl", logsRoot);
        const PacketHeader header = makeHeader(PacketType::TELEMETRY, "AC-123", 7, sizeof(TelemetryPayload), 99);
        logger.logPacket("TX", header);
    }

    const std::string contents = readFile(onlyFileIn(logsRoot / "groundctrl_comms"));
    EXPECT_NE(contents.find("[TX] [TELEMETRY]"), std::string::npos);
    EXPECT_NE(contents.find("Aircraft=AC-123"), std::string::npos);
    EXPECT_NE(contents.find("Seq=0007"), std::string::npos);
    EXPECT_NE(contents.find("PayloadSize=20"), std::string::npos);
}

TEST(LoggerTest, FaultMirrorsToBlackboxForGroundRole) {
    const std::filesystem::path logsRoot = makeTempDirectory("logger_fault");

    {
        Logger logger("groundctrl", logsRoot);
        logger.logFault("ReceiveFailure", "STATE_TELEMETRY", 6);
        logger.logInfo("Retained in comms log only");
    }

    const std::string commsContents = readFile(onlyFileIn(logsRoot / "groundctrl_comms"));
    const std::string blackboxContents = readFile(onlyFileIn(logsRoot / "blackbox"));
    EXPECT_NE(commsContents.find("[FAULT] Cause=ReceiveFailure State=STATE_TELEMETRY Seq=0006"), std::string::npos);
    EXPECT_NE(blackboxContents.find("[FAULT] Cause=ReceiveFailure State=STATE_TELEMETRY Seq=0006"), std::string::npos);
    EXPECT_NE(commsContents.find("[INFO] Retained in comms log only"), std::string::npos);
    EXPECT_EQ(blackboxContents.find("[INFO]"), std::string::npos);
}

TEST(LoggerTest, AircraftFaultStaysInAircraftCommsLog) {
    const std::filesystem::path logsRoot = makeTempDirectory("logger_aircraft_fault");

    {
        Logger logger("aircraft", logsRoot);
        logger.logFault("TelemetrySendFailure", "TELEMETRY", 12);
    }

    const std::string commsContents = readFile(onlyFileIn(logsRoot / "aircraft_comms"));
    EXPECT_NE(commsContents.find("Cause=TelemetrySendFailure"), std::string::npos);
    EXPECT_FALSE(std::filesystem::exists(logsRoot / "blackbox"));
}
