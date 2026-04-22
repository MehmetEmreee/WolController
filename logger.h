/**
 * @file logger.h
 * @brief Structured serial logging with severity levels, module tags,
 *        and optional remote output via callback.
 *
 * Provides a zero-allocation logging interface that formats output as:
 *   [LEVEL] [module] message
 *
 * All log functions use stack-allocated buffers with bounded snprintf to
 * prevent heap fragmentation in the hot path.
 *
 * Remote output: A callback can be registered via setRemoteOutput() to
 * mirror log lines to a remote sink (e.g., telnet server). This avoids
 * circular header dependencies between Logger and RemoteLog.
 *
 * @note Serial must be initialized (Serial.begin) before calling any
 *       log function.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include "config.h"

/**
 * @brief Log severity levels.
 */
enum class LogLevel : uint8_t {
    INFO  = 0,  ///< Informational messages (normal operation)
    WARN  = 1,  ///< Warning conditions (recoverable anomalies)
    ERROR = 2   ///< Error conditions (action required)
};

/**
 * @brief Function pointer type for remote log output.
 *
 * The function receives the fully formatted log line (without trailing newline).
 * It must be non-blocking and fast.
 */
using RemoteOutputFn = void(*)(const char* line);

/**
 * @brief Structured logger singleton.
 *
 * Formats and emits log lines to Serial with consistent formatting.
 * Optionally mirrors output to a remote sink via registered callback.
 * Thread-safe only in single-core cooperative context (no RTOS tasks).
 *
 * Justified singleton: Single Serial resource, minimal mutable state
 * (only the optional remote output callback pointer).
 */
class Logger {
public:
    /**
     * @brief Get the singleton Logger instance.
     * @return Reference to the global Logger.
     */
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    /**
     * @brief Initialize the serial interface for logging.
     * @param baud Baud rate for Serial communication.
     */
    void begin(unsigned long baud = 115200) {
        Serial.begin(baud);
        while (!Serial && millis() < 3000) {
            // Wait up to 3s for serial connection (USB CDC)
        }
    }

    /**
     * @brief Register a remote output callback.
     *
     * Once set, all log lines will be mirrored to this function in addition
     * to Serial. Pass nullptr to disable remote output.
     *
     * @param fn Function pointer to call with each formatted log line.
     */
    void setRemoteOutput(RemoteOutputFn fn) {
        remoteOutputFn_ = fn;
    }

    /**
     * @brief Log an informational message.
     * @param module Source module tag (e.g., "network", "watchdog").
     * @param fmt    printf-style format string.
     * @param ...    Format arguments.
     */
    void info(const char* module, const char* fmt, ...) __attribute__((format(printf, 3, 4))) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::INFO, module, fmt, args);
        va_end(args);
    }

    /**
     * @brief Log a warning message.
     * @param module Source module tag.
     * @param fmt    printf-style format string.
     * @param ...    Format arguments.
     */
    void warn(const char* module, const char* fmt, ...) __attribute__((format(printf, 3, 4))) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::WARN, module, fmt, args);
        va_end(args);
    }

    /**
     * @brief Log an error message.
     * @param module Source module tag.
     * @param fmt    printf-style format string.
     * @param ...    Format arguments.
     */
    void error(const char* module, const char* fmt, ...) __attribute__((format(printf, 3, 4))) {
        va_list args;
        va_start(args, fmt);
        log(LogLevel::ERROR, module, fmt, args);
        va_end(args);
    }

    // Delete copy/move constructors and assignment operators
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

private:
    Logger() = default;

    /** @brief Optional remote output callback (nullptr = disabled). */
    RemoteOutputFn remoteOutputFn_ = nullptr;

    /**
     * @brief Convert LogLevel enum to fixed-width string for formatting.
     * @param level The log severity level.
     * @return Pointer to a static string representation.
     */
    static const char* levelToStr(LogLevel level) {
        switch (level) {
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            default:              return "?????";
        }
    }

    /**
     * @brief Core log formatting and emission.
     *
     * Uses stack-allocated buffers with bounded vsnprintf. No heap allocation.
     * Outputs to Serial first, then to the remote callback if registered.
     *
     * @param level  Severity level.
     * @param module Source module tag.
     * @param fmt    Format string.
     * @param args   Variadic argument list.
     */
    void log(LogLevel level, const char* module, const char* fmt, va_list args) {
        // Format the user message portion
        char msgBuf[LOG_MSG_MAX_LEN];
        int written = vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
        if (written < 0) {
            msgBuf[0] = '\0';
        }

        // Format the complete log line: [LEVEL] [module] message
        char lineBuf[LOG_MSG_MAX_LEN + 32];
        snprintf(lineBuf, sizeof(lineBuf), "[%s] [%s] %s",
                 levelToStr(level), module, msgBuf);

        // Emit to Serial
        Serial.println(lineBuf);

        // Emit to remote output if registered
        if (remoteOutputFn_) {
            remoteOutputFn_(lineBuf);
        }
    }
};

// ============================================================================
//  Convenience macros for concise logging in application code
// ============================================================================

/** @brief Log an INFO-level message. Usage: LOG_INFO("module", "fmt", ...) */
#define LOG_INFO(module, ...)  Logger::instance().info(module, __VA_ARGS__)

/** @brief Log a WARN-level message. Usage: LOG_WARN("module", "fmt", ...) */
#define LOG_WARN(module, ...)  Logger::instance().warn(module, __VA_ARGS__)

/** @brief Log an ERROR-level message. Usage: LOG_ERROR("module", "fmt", ...) */
#define LOG_ERROR(module, ...) Logger::instance().error(module, __VA_ARGS__)

#endif // LOGGER_H
