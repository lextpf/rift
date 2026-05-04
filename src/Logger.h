#pragma once

#include <cstdint>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

/**
 * @enum LogLevel
 * @brief Severity levels for Logger entries, ordered by ascending priority.
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Each level maps to a tag printed in the log line and to an ANSI color used
 * for the console mirror. Calls below the configured minimum level
 * (Logger::SetMinLevel) are dropped at the entry point with no formatting cost.
 *
 * | Level   | Tag       | Console Color    | Typical Use                      |
 * |---------|-----------|------------------|----------------------------------|
 * | Trace   | `[TRACE]` | Dim white        | Very verbose flow tracing        |
 * | Debug   | `[DEBUG]` | Cyan             | Diagnostic detail for developers |
 * | Info    | `[INFO] ` | Bright blue      | Normal status messages           |
 * | Warn    | `[WARN] ` | Yellow           | Recoverable problems             |
 * | Error   | `[ERROR]` | Bright red       | Failed operations, kept running  |
 * | Fatal   | `[FATAL]` | White on red     | About-to-terminate condition     |
 */
enum class LogLevel : std::uint8_t
{
    Trace,  ///< Verbose flow tracing (dim white).
    Debug,  ///< Diagnostic detail for development (cyan).
    Info,   ///< Normal status messages (bright blue).
    Warn,   ///< Recoverable problems (yellow).
    Error,  ///< Failed operations the program survived (bright red).
    Fatal   ///< About to terminate (white on red).
};

/**
 * @class Logger
 * @brief Engine-wide leveled logger with timestamped, subsystem-tagged output
 *        mirrored to console (ANSI-colored) and `rift.project.log` (plain text).
 * @author Alex (https://github.com/lextpf)
 * @ingroup Core
 *
 * Logger is a static class with no instances. Every call has the form
 * `Logger::Info(subsystem, message)` and produces one line in both the console
 * (with ANSI color escapes) and the log file (plain text).
 *
 * @par Output Format
 * @code
 * [HH:MM:SS.mmm] [LEVEL]  [Subsys]    message
 * @endcode
 * - The level field is right-padded to 7 characters (`[ERROR]`-width).
 * - The subsystem field is right-padded to 10 characters (`[Dialogue]`-width).
 * - The message body is whatever the caller passed.
 *
 * @par Initialization Order
 * Must be Initialized AFTER `AllocConsole()` + `freopen_s` redirects so the
 * VT-processing toggle has the post-redirect console handle. Must be Shutdown
 * before `main()` returns to flush the file. See `src/main.cpp`.
 *
 * @par Crash Handler Coexistence
 * The signal/SEH handlers in `main.cpp` write the same file
 * (@ref LOG_FILE_PATH) via async-signal-safe `_open`/`_write`/`_close` and
 * CANNOT call into Logger. Both ends use the same literal path so entries
 * interleave correctly, and the crash handlers always open with `_O_APPEND`.
 *
 * @par Thread Safety
 * Single-threaded by design. The rest of the engine has no `std::mutex` or
 * `std::atomic`; Logger matches that contract. Calling from multiple threads
 * concurrently is a data race.
 *
 * @par Format Helpers
 * `*F` overloads accept C++23 `std::format` strings:
 * @code
 * Logger::InfoF("Tilemap", "Loaded '{}' ({}x{})", path, width, height);
 * @endcode
 * The plain overloads accept any `std::string_view` and are used when the
 * caller already has a fully-built string.
 *
 * @par Per-File Subsystem Convention
 * Each `.cpp` declares a constant at the top of an anonymous namespace and
 * passes it to every Logger call:
 * @code
 * namespace
 * {
 * constexpr const char* LOG_SUBSYSTEM = "Game";
 * }
 *
 * void DoStuff()
 * {
 *     Logger::Info(LOG_SUBSYSTEM, "Doing stuff");
 * }
 * @endcode
 *
 * @par Failure Semantics
 * If `Initialize()` cannot open the file or enable virtual-terminal processing,
 * it returns `false` but leaves Logger in a working state: subsequent calls
 * are dropped silently rather than throwing. Crashes still produce output
 * because the signal handlers do not depend on Logger state.
 *
 * @see LogLevel
 */
class Logger
{
public:
    Logger() = delete;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// @name Constants
    /// @{

    /**
     * @brief Path of the log file, relative to the working directory.
     *
     * Signal/SEH handlers cannot call methods of this class, so they embed
     * the same literal. Keep them in sync if the path ever changes.
     */
    static constexpr const char* LOG_FILE_PATH = "rift.project.log";

    /// @}

    /// @name Lifecycle
    /// @{

    /**
     * @brief Open the log file (truncate), enable VT processing on console.
     *
     * Idempotent: a second call is a no-op. Safe to call before
     * `AllocConsole()` (color toggle will silently fail and ANSI codes will
     * be omitted from console output).
     *
     * @return true if the file was opened successfully, false otherwise.
     *         Either way, subsequent log calls are safe.
     */
    static bool Initialize();

    /**
     * @brief Flush and close the log file.
     *
     * Idempotent. After Shutdown, log calls are silently dropped.
     */
    static void Shutdown();

    /// @}

    /// @name Configuration
    /// @{

    /**
     * @brief Set the minimum severity level emitted.
     *
     * Calls with lower severity are dropped at the entry point with no
     * formatting cost.
     *
     * @param level Minimum severity to emit (default: Trace, emit everything).
     */
    static void SetMinLevel(LogLevel level);

    /**
     * @brief Get the current minimum severity level.
     * @return The level configured by SetMinLevel.
     */
    static LogLevel GetMinLevel();

    /**
     * @brief Check whether Logger has been initialized successfully.
     * @return true if Initialize() returned true and Shutdown() has not run.
     */
    static bool IsInitialized();

    /// @}

    /// @name Plain-Message Emitters
    /// @brief One method per LogLevel; the message is written verbatim.
    /// @{

    /// @brief Emit a Trace-level entry.
    static void Trace(std::string_view subsystem, std::string_view message);

    /// @brief Emit a Debug-level entry.
    static void Debug(std::string_view subsystem, std::string_view message);

    /// @brief Emit an Info-level entry.
    static void Info(std::string_view subsystem, std::string_view message);

    /// @brief Emit a Warn-level entry.
    static void Warn(std::string_view subsystem, std::string_view message);

    /// @brief Emit an Error-level entry.
    static void Error(std::string_view subsystem, std::string_view message);

    /// @brief Emit a Fatal-level entry.
    static void Fatal(std::string_view subsystem, std::string_view message);

    /// @brief Emit an entry at the specified level.
    static void Log(LogLevel level, std::string_view subsystem, std::string_view message);

    /// @}

    /// @name std::format Emitters
    /// @brief Variadic helpers that accept a `std::format` string.
    /// @{

    /// @brief Emit a Trace-level entry built from a format string.
    template <typename... Args>
    static void TraceF(std::string_view subsystem, std::format_string<Args...> fmt, Args&&... args)
    {
        Trace(subsystem, std::format(fmt, std::forward<Args>(args)...));
    }

    /// @brief Emit a Debug-level entry built from a format string.
    template <typename... Args>
    static void DebugF(std::string_view subsystem, std::format_string<Args...> fmt, Args&&... args)
    {
        Debug(subsystem, std::format(fmt, std::forward<Args>(args)...));
    }

    /// @brief Emit an Info-level entry built from a format string.
    template <typename... Args>
    static void InfoF(std::string_view subsystem, std::format_string<Args...> fmt, Args&&... args)
    {
        Info(subsystem, std::format(fmt, std::forward<Args>(args)...));
    }

    /// @brief Emit a Warn-level entry built from a format string.
    template <typename... Args>
    static void WarnF(std::string_view subsystem, std::format_string<Args...> fmt, Args&&... args)
    {
        Warn(subsystem, std::format(fmt, std::forward<Args>(args)...));
    }

    /// @brief Emit an Error-level entry built from a format string.
    template <typename... Args>
    static void ErrorF(std::string_view subsystem, std::format_string<Args...> fmt, Args&&... args)
    {
        Error(subsystem, std::format(fmt, std::forward<Args>(args)...));
    }

    /// @brief Emit a Fatal-level entry built from a format string.
    template <typename... Args>
    static void FatalF(std::string_view subsystem, std::format_string<Args...> fmt, Args&&... args)
    {
        Fatal(subsystem, std::format(fmt, std::forward<Args>(args)...));
    }

    /// @}

    /// @name Field Widths
    /// @brief Tag-field padding constants used by Emit() and exposed for tests.
    /// @{

    /// @brief Width of the bracketed level tag, e.g. `[ERROR]` = 7 chars.
    static constexpr std::size_t LEVEL_FIELD_WIDTH = 7;

    /// @brief Width of the bracketed subsystem tag, e.g. `[Dialogue]` = 10 chars.
    static constexpr std::size_t SUBSYSTEM_FIELD_WIDTH = 10;

    /// @brief Maximum subsystem name length that fits without truncation
    ///        (`SUBSYSTEM_FIELD_WIDTH` minus the two enclosing brackets).
    static constexpr std::size_t MAX_SUBSYSTEM_NAME_LENGTH = SUBSYSTEM_FIELD_WIDTH - 2;

    /// @}

    /// @name Test Hooks
    /// @brief Internal helpers exposed for unit tests; not for production callers.
    /// @{

    /**
     * @brief Format the level tag with bracket padding (e.g. `"[INFO] "`).
     *
     * Returns a 7-character string view including any trailing space needed
     * to align with the longest tag, `[ERROR]` / `[FATAL]`.
     *
     * @param level Severity level.
     * @return Padded tag of width @ref LEVEL_FIELD_WIDTH.
     */
    static std::string_view LevelTagPadded(LogLevel level);

    /**
     * @brief Format the subsystem tag with bracket padding.
     *
     * The result is at most @ref SUBSYSTEM_FIELD_WIDTH characters; longer
     * subsystem names are truncated to `MAX_SUBSYSTEM_NAME_LENGTH`.
     *
     * @param subsystem Subsystem name (without brackets).
     * @return Padded tag of width @ref SUBSYSTEM_FIELD_WIDTH.
     */
    static std::string SubsystemTagPadded(std::string_view subsystem);

    /// @}

private:
    /**
     * @brief Format a message and emit it to file and console.
     *
     * The caller is expected to have already filtered by SetMinLevel.
     */
    static void Emit(LogLevel level, std::string_view subsystem, std::string_view message);

    /**
     * @brief Build the `[HH:MM:SS.mmm]` timestamp string for the current moment.
     */
    static std::string FormatTimestamp();

    /**
     * @brief Get the ANSI color escape for the given level, or `""` if VT is
     *        disabled or color is not desired.
     */
    static const char* AnsiColorFor(LogLevel level);

    /**
     * @brief Try to enable `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on stdout/stderr.
     * @return true if both handles were updated successfully.
     */
    static bool TryEnableVirtualTerminal();

    static LogLevel s_MinLevel;
    static bool s_Initialized;
    static bool s_VtEnabled;
    static std::ofstream s_File;
};
