#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#include "../src/Logger.hpp"

namespace
{
std::string ReadLogFile()
{
    std::ifstream in(Logger::LOG_FILE_PATH, std::ios::in | std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
}  // namespace

class LoggerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Logger::Shutdown();
        std::remove(Logger::LOG_FILE_PATH);
        ASSERT_TRUE(Logger::Initialize());
        Logger::SetMinLevel(LogLevel::Trace);
    }

    void TearDown() override
    {
        Logger::Shutdown();
        std::remove(Logger::LOG_FILE_PATH);
    }
};

TEST_F(LoggerTest, FormatsTimestampLevelAndSubsystem)
{
    Logger::Info("Game", "hello");
    Logger::Shutdown();

    const std::string content = ReadLogFile();
    const std::regex pattern(
        R"(^\[\d{2}:\d{2}:\d{2}\.\d{3}\] \[INFO\] {1,2}\[Game\] +hello\r?\n$)");
    EXPECT_TRUE(std::regex_match(content, pattern)) << "Actual: " << content;
}

TEST_F(LoggerTest, EachLevelEmitsCorrectTag)
{
    Logger::Trace("Test", "trace-msg");
    Logger::Debug("Test", "debug-msg");
    Logger::Info("Test", "info-msg");
    Logger::Warn("Test", "warn-msg");
    Logger::Error("Test", "error-msg");
    Logger::Fatal("Test", "fatal-msg");
    Logger::Shutdown();

    const std::string content = ReadLogFile();
    EXPECT_NE(content.find("[TRACE] [Test]"), std::string::npos);
    EXPECT_NE(content.find("[DEBUG] [Test]"), std::string::npos);
    EXPECT_NE(content.find("[INFO]  [Test]"), std::string::npos);
    EXPECT_NE(content.find("[WARN]  [Test]"), std::string::npos);
    EXPECT_NE(content.find("[ERROR] [Test]"), std::string::npos);
    EXPECT_NE(content.find("[FATAL] [Test]"), std::string::npos);
    EXPECT_NE(content.find("trace-msg"), std::string::npos);
    EXPECT_NE(content.find("debug-msg"), std::string::npos);
    EXPECT_NE(content.find("info-msg"), std::string::npos);
    EXPECT_NE(content.find("warn-msg"), std::string::npos);
    EXPECT_NE(content.find("error-msg"), std::string::npos);
    EXPECT_NE(content.find("fatal-msg"), std::string::npos);
}

TEST_F(LoggerTest, MinLevelSuppressesBelow)
{
    Logger::SetMinLevel(LogLevel::Warn);

    Logger::Trace("Test", "trace-msg");
    Logger::Debug("Test", "debug-msg");
    Logger::Info("Test", "info-msg");
    Logger::Warn("Test", "warn-msg");
    Logger::Error("Test", "error-msg");
    Logger::Shutdown();

    const std::string content = ReadLogFile();
    EXPECT_EQ(content.find("trace-msg"), std::string::npos);
    EXPECT_EQ(content.find("debug-msg"), std::string::npos);
    EXPECT_EQ(content.find("info-msg"), std::string::npos);
    EXPECT_NE(content.find("warn-msg"), std::string::npos);
    EXPECT_NE(content.find("error-msg"), std::string::npos);
}

TEST_F(LoggerTest, LevelTagPaddedToSevenChars)
{
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Trace).size(), Logger::LEVEL_FIELD_WIDTH);
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Debug).size(), Logger::LEVEL_FIELD_WIDTH);
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Info).size(), Logger::LEVEL_FIELD_WIDTH);
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Warn).size(), Logger::LEVEL_FIELD_WIDTH);
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Error).size(), Logger::LEVEL_FIELD_WIDTH);
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Fatal).size(), Logger::LEVEL_FIELD_WIDTH);

    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Info), "[INFO] ");
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Warn), "[WARN] ");
    EXPECT_EQ(Logger::LevelTagPadded(LogLevel::Error), "[ERROR]");
}

TEST_F(LoggerTest, SubsystemTagPaddedToTenChars)
{
    EXPECT_EQ(Logger::SubsystemTagPadded("Game").size(), Logger::SUBSYSTEM_FIELD_WIDTH);
    EXPECT_EQ(Logger::SubsystemTagPadded("Renderer").size(), Logger::SUBSYSTEM_FIELD_WIDTH);
    EXPECT_EQ(Logger::SubsystemTagPadded("Dialogue").size(), Logger::SUBSYSTEM_FIELD_WIDTH);

    EXPECT_EQ(Logger::SubsystemTagPadded("Game"), "[Game]    ");
    EXPECT_EQ(Logger::SubsystemTagPadded("Dialogue"), "[Dialogue]");
}

TEST_F(LoggerTest, SubsystemTagTruncatesOverlongNames)
{
    const std::string padded = Logger::SubsystemTagPadded("VeryLongSubsystemName");
    EXPECT_EQ(padded.size(), Logger::SUBSYSTEM_FIELD_WIDTH);
    EXPECT_EQ(padded[0], '[');
    EXPECT_EQ(padded[padded.size() - 1], ']');
}

TEST_F(LoggerTest, FormatVariadicWorks)
{
    Logger::InfoF("Game", "x={} y={}", 1, 2);
    Logger::Shutdown();

    const std::string content = ReadLogFile();
    EXPECT_NE(content.find("x=1 y=2"), std::string::npos) << "Actual: " << content;
}

TEST_F(LoggerTest, AllFormatVariadicHelpersWork)
{
    Logger::TraceF("S", "trace-{}", 1);
    Logger::DebugF("S", "debug-{}", 2);
    Logger::InfoF("S", "info-{}", 3);
    Logger::WarnF("S", "warn-{}", 4);
    Logger::ErrorF("S", "error-{}", 5);
    Logger::FatalF("S", "fatal-{}", 6);
    Logger::Shutdown();

    const std::string content = ReadLogFile();
    EXPECT_NE(content.find("trace-1"), std::string::npos);
    EXPECT_NE(content.find("debug-2"), std::string::npos);
    EXPECT_NE(content.find("info-3"), std::string::npos);
    EXPECT_NE(content.find("warn-4"), std::string::npos);
    EXPECT_NE(content.find("error-5"), std::string::npos);
    EXPECT_NE(content.find("fatal-6"), std::string::npos);
}

TEST_F(LoggerTest, LogFileTruncatedOnInitialize)
{
    Logger::Info("Test", "first-run-line");
    Logger::Shutdown();

    {
        const std::string before = ReadLogFile();
        EXPECT_NE(before.find("first-run-line"), std::string::npos);
    }

    ASSERT_TRUE(Logger::Initialize());
    Logger::Info("Test", "second-run-line");
    Logger::Shutdown();

    const std::string after = ReadLogFile();
    EXPECT_EQ(after.find("first-run-line"), std::string::npos)
        << "Initialize should have truncated.";
    EXPECT_NE(after.find("second-run-line"), std::string::npos);
}

TEST_F(LoggerTest, MessageBodyPreservedExactly)
{
    Logger::Info("Test", "embedded [brackets] and \"quotes\" and: 123");
    Logger::Shutdown();

    const std::string content = ReadLogFile();
    EXPECT_NE(content.find("embedded [brackets] and \"quotes\" and: 123"), std::string::npos);
}

TEST_F(LoggerTest, GetMinLevelMatchesSet)
{
    Logger::SetMinLevel(LogLevel::Error);
    EXPECT_EQ(Logger::GetMinLevel(), LogLevel::Error);
    Logger::SetMinLevel(LogLevel::Trace);
    EXPECT_EQ(Logger::GetMinLevel(), LogLevel::Trace);
}

TEST_F(LoggerTest, IsInitializedReflectsLifecycle)
{
    EXPECT_TRUE(Logger::IsInitialized());
    Logger::Shutdown();
    EXPECT_FALSE(Logger::IsInitialized());
    ASSERT_TRUE(Logger::Initialize());
    EXPECT_TRUE(Logger::IsInitialized());
}

TEST_F(LoggerTest, ShutdownIsIdempotent)
{
    Logger::Shutdown();
    Logger::Shutdown();
    EXPECT_FALSE(Logger::IsInitialized());
}

TEST_F(LoggerTest, InitializeIsIdempotent)
{
    EXPECT_TRUE(Logger::Initialize());
    EXPECT_TRUE(Logger::IsInitialized());
}
