#include "Logger.h"

#include <array>
#include <chrono>
#include <ctime>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace
{
constexpr const char* ANSI_RESET = "\x1b[0m";
constexpr const char* ANSI_TIMESTAMP = "\x1b[90m";     // Bright black (grey)
constexpr const char* ANSI_BODY_DEFAULT = "\x1b[37m";  // White (info / debug / trace bodies)
constexpr const char* ANSI_TRACE = "\x1b[2;37m";       // Dim white
constexpr const char* ANSI_DEBUG = "\x1b[36m";         // Cyan
constexpr const char* ANSI_INFO = "\x1b[94m";          // Bright blue
constexpr const char* ANSI_WARN = "\x1b[33m";          // Yellow
constexpr const char* ANSI_ERROR = "\x1b[91m";         // Bright red
constexpr const char* ANSI_FATAL = "\x1b[97;41m";      // White on red

// Subsystem palette - 24 readable 256-colour codes spread across the cool
// half of the spectrum (greens / cyans / blues / magentas / pinks). Yellow
// and red are deliberately omitted so the subsystem tag never collides with
// the WARN / ERROR / FATAL body colours.
constexpr const char* SUBSYSTEM_PALETTE[] = {
    "\x1b[38;5;39m",   // azure blue
    "\x1b[38;5;41m",   // spring green
    "\x1b[38;5;43m",   // turquoise
    "\x1b[38;5;45m",   // bright turquoise
    "\x1b[38;5;75m",   // sky blue
    "\x1b[38;5;77m",   // pale green
    "\x1b[38;5;81m",   // pale turquoise
    "\x1b[38;5;105m",  // light slate blue
    "\x1b[38;5;113m",  // medium spring green
    "\x1b[38;5;120m",  // light green
    "\x1b[38;5;135m",  // medium purple
    "\x1b[38;5;141m",  // medium lavender
    "\x1b[38;5;147m",  // light lavender
    "\x1b[38;5;156m",  // pale yellow-green
    "\x1b[38;5;159m",  // pale azure
    "\x1b[38;5;165m",  // magenta
    "\x1b[38;5;171m",  // bright magenta
    "\x1b[38;5;177m",  // orchid
    "\x1b[38;5;180m",  // tan
    "\x1b[38;5;183m",  // plum
    "\x1b[38;5;195m",  // light cyan
    "\x1b[38;5;207m",  // pink
    "\x1b[38;5;213m",  // light pink
    "\x1b[38;5;218m",  // pale rose
};
constexpr std::size_t SUBSYSTEM_PALETTE_SIZE =
    sizeof(SUBSYSTEM_PALETTE) / sizeof(SUBSYSTEM_PALETTE[0]);

constexpr std::string_view TAG_TRACE = "[TRACE]";
constexpr std::string_view TAG_DEBUG = "[DEBUG]";
constexpr std::string_view TAG_INFO = "[INFO] ";
constexpr std::string_view TAG_WARN = "[WARN] ";
constexpr std::string_view TAG_ERROR = "[ERROR]";
constexpr std::string_view TAG_FATAL = "[FATAL]";

const char* AnsiColorForSubsystem(std::string_view subsystem)
{
    // Stable FNV-style hash so a given subsystem name always picks the same
    // palette slot across runs.
    std::size_t hash = 2166136261u;
    for (char c : subsystem)
    {
        hash ^= static_cast<unsigned char>(c);
        hash *= 16777619u;
    }
    return SUBSYSTEM_PALETTE[hash % SUBSYSTEM_PALETTE_SIZE];
}

const char* AnsiBodyColorFor(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:
        case LogLevel::Debug:
        case LogLevel::Info:
            return ANSI_BODY_DEFAULT;
        case LogLevel::Warn:
            return ANSI_WARN;
        case LogLevel::Error:
            return ANSI_ERROR;
        case LogLevel::Fatal:
            return ANSI_FATAL;
    }
    return ANSI_BODY_DEFAULT;
}
}  // namespace

LogLevel Logger::s_MinLevel = LogLevel::Trace;
bool Logger::s_Initialized = false;
bool Logger::s_VtEnabled = false;
std::ofstream Logger::s_File;

bool Logger::Initialize()
{
    if (s_Initialized)
    {
        return true;
    }

    s_File.open(LOG_FILE_PATH, std::ios::out | std::ios::trunc);
    const bool fileOpen = s_File.is_open();

    s_VtEnabled = TryEnableVirtualTerminal();
    s_Initialized = true;

    return fileOpen;
}

void Logger::Shutdown()
{
    if (!s_Initialized)
    {
        return;
    }

    if (s_File.is_open())
    {
        s_File.flush();
        s_File.close();
    }

    s_Initialized = false;
    s_VtEnabled = false;
}

bool Logger::IsInitialized()
{
    return s_Initialized;
}

void Logger::SetMinLevel(LogLevel level)
{
    s_MinLevel = level;
}

LogLevel Logger::GetMinLevel()
{
    return s_MinLevel;
}

void Logger::Trace(std::string_view subsystem, std::string_view message)
{
    Log(LogLevel::Trace, subsystem, message);
}

void Logger::Debug(std::string_view subsystem, std::string_view message)
{
    Log(LogLevel::Debug, subsystem, message);
}

void Logger::Info(std::string_view subsystem, std::string_view message)
{
    Log(LogLevel::Info, subsystem, message);
}

void Logger::Warn(std::string_view subsystem, std::string_view message)
{
    Log(LogLevel::Warn, subsystem, message);
}

void Logger::Error(std::string_view subsystem, std::string_view message)
{
    Log(LogLevel::Error, subsystem, message);
}

void Logger::Fatal(std::string_view subsystem, std::string_view message)
{
    Log(LogLevel::Fatal, subsystem, message);
}

void Logger::Log(LogLevel level, std::string_view subsystem, std::string_view message)
{
    if (static_cast<std::uint8_t>(level) < static_cast<std::uint8_t>(s_MinLevel))
    {
        return;
    }
    Emit(level, subsystem, message);
}

std::string_view Logger::LevelTagPadded(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:
            return TAG_TRACE;
        case LogLevel::Debug:
            return TAG_DEBUG;
        case LogLevel::Info:
            return TAG_INFO;
        case LogLevel::Warn:
            return TAG_WARN;
        case LogLevel::Error:
            return TAG_ERROR;
        case LogLevel::Fatal:
            return TAG_FATAL;
    }
    return TAG_INFO;
}

std::string Logger::SubsystemTagPadded(std::string_view subsystem)
{
    std::string_view name = subsystem;
    if (name.size() > MAX_SUBSYSTEM_NAME_LENGTH)
    {
        name = name.substr(0, MAX_SUBSYSTEM_NAME_LENGTH);
    }

    std::string out;
    out.reserve(SUBSYSTEM_FIELD_WIDTH);
    out.push_back('[');
    out.append(name);
    out.push_back(']');
    if (out.size() < SUBSYSTEM_FIELD_WIDTH)
    {
        out.append(SUBSYSTEM_FIELD_WIDTH - out.size(), ' ');
    }
    return out;
}

void Logger::Emit(LogLevel level, std::string_view subsystem, std::string_view message)
{
    const std::string timestamp = FormatTimestamp();
    const std::string_view levelTag = LevelTagPadded(level);
    const std::string subsystemTag = SubsystemTagPadded(subsystem);

    const std::string fileLine =
        std::format("{} {} {} {}\n", timestamp, levelTag, subsystemTag, message);

    if (s_File.is_open())
    {
        s_File << fileLine;
        s_File.flush();
    }

    std::ostream& out =
        (static_cast<std::uint8_t>(level) >= static_cast<std::uint8_t>(LogLevel::Warn)) ? std::cerr
                                                                                        : std::cout;

    if (s_VtEnabled)
    {
        const char* levelColor = AnsiColorFor(level);
        const char* subsysColor = AnsiColorForSubsystem(subsystem);
        const char* bodyColor = AnsiBodyColorFor(level);
        out << ANSI_TIMESTAMP << timestamp << ANSI_RESET << ' ' << levelColor << levelTag
            << ANSI_RESET << ' ' << subsysColor << subsystemTag << ANSI_RESET << ' ' << bodyColor
            << message << ANSI_RESET << '\n';
    }
    else
    {
        out << fileLine;
    }
    out.flush();
}

std::string Logger::FormatTimestamp()
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    const auto seconds = time_point_cast<std::chrono::seconds>(now);
    const auto millis = duration_cast<milliseconds>(now - seconds).count();

    const std::time_t t = system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif

    return std::format("[{:02}:{:02}:{:02}.{:03}]",
                       local.tm_hour,
                       local.tm_min,
                       local.tm_sec,
                       static_cast<int>(millis));
}

const char* Logger::AnsiColorFor(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace:
            return ANSI_TRACE;
        case LogLevel::Debug:
            return ANSI_DEBUG;
        case LogLevel::Info:
            return ANSI_INFO;
        case LogLevel::Warn:
            return ANSI_WARN;
        case LogLevel::Error:
            return ANSI_ERROR;
        case LogLevel::Fatal:
            return ANSI_FATAL;
    }
    return "";
}

bool Logger::TryEnableVirtualTerminal()
{
#ifdef _WIN32
    auto enableForHandle = [](DWORD handleId) -> bool
    {
        HANDLE handle = GetStdHandle(handleId);
        if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
        {
            return false;
        }
        DWORD mode = 0;
        if (!GetConsoleMode(handle, &mode))
        {
            return false;
        }
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        return SetConsoleMode(handle, mode) != 0;
    };

    const bool stdoutOk = enableForHandle(STD_OUTPUT_HANDLE);
    const bool stderrOk = enableForHandle(STD_ERROR_HANDLE);
    return stdoutOk && stderrOk;
#else
    return true;
#endif
}
