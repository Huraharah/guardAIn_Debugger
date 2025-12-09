#pragma once
#pragma once

#include <string>
#include <mutex>
#include <ostream>

enum class LogLevel {
    Debug = 0,
    Info,
    Warn,
    Error
};

class Logger {
public:
    // Call this once early in main() if you want a log file.
    // If not called, logs go only to stdout.
    static void setLogFile(const std::string& filePath);

    // Minimum level to actually emit. Default: Info.
    static void setMinLevel(LogLevel level);

    // Core logging entrypoint
    static void log(LogLevel level, const std::string& message);

    // Convenience helpers
    static void debug(const std::string& message) { log(LogLevel::Debug, message); }
    static void info(const std::string& message) { log(LogLevel::Info, message); }
    static void warn(const std::string& message) { log(LogLevel::Warn, message); }
    static void error(const std::string& message) { log(LogLevel::Error, message); }

private:
    static std::mutex& getMutex();
    static std::ostream& getStdout();
    static LogLevel& getMinLevelRef();
    static std::ostream* getFileStream();
    static void              setFileStream(std::ostream* os);

    static const char* levelToString(LogLevel level);
    static std::string currentTimestamp();
};
