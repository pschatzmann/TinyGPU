#pragma once
#include <cstdarg>
#include <cstdio>

namespace tinygpu {

/**
 * @class TinyGPULoggerClass
 * @brief Simple header-only logger for TinyGPU with log levels and vararg
 * support.
 *
 * This logger provides printf-style logging with selectable log levels.
 * Usage example:
 *   TinyGPULogger.setLevel(TinyGPULogger::DEBUG);
 *   TinyGPULogger.log(TinyGPULogger::INFO, "Hello %d", 42);
 *
 * Log levels:
 *   - DEBUG: Detailed debug information
 *   - INFO:  General information
 *   - WARN:  Warnings
 *   - ERROR: Errors only
 *   - NONE:  Disable all logging
 */
class TinyGPULoggerClass {
 public:
  /**
   * @enum Level
   * @brief Logging levels for TinyGPULogger
   */
  enum Level {
    DEBUG = 0,  ///< Detailed debug information
    INFO,       ///< General information
    WARN,       ///< Warnings
    ERROR,      ///< Errors only
    NONE        ///< Disable all logging
  };

  /**
   * @brief Set the current log level.
   * @param level The minimum log level to output.
   */
  void setLevel(Level level) { currentLevel() = level; }

  /**
   * @brief Get the current log level.
   * @return The current log level.
   */
  Level getLevel() { return currentLevel(); }

  /**
   * @brief Log a message with printf-style formatting.
   * @param level Log level for this message.
   * @param fmt Format string (like printf).
   * @param ... Arguments for the format string.
   */
  void log(Level level, const char* fmt, ...) {
    if (level < currentLevel() || level == NONE) return;
    va_list args;
    va_start(args, fmt);
    vlog(level, fmt, args);
    va_end(args);
  }

 private:
  Level lvl = INFO;

  Level& currentLevel() { return lvl; }

  void vlog(Level level, const char* fmt, va_list args) {
    const char* levelNames[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    if (level < DEBUG || level > ERROR) level = ERROR;
    printf("[%s] ", levelNames[level]);
    vprintf(fmt, args);
    printf("\n");
  }
};

/// Global instance of TinyGPULogger
static TinyGPULoggerClass TinyGPULogger;

}  // namespace tinygpu