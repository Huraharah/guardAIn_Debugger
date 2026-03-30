#include "Logger.h"

#include <iostream>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

// Static storage via function-local statics to avoid ODR/linker headaches.

std::mutex& Logger::getMutex() {
    static std::mutex m;
    return m;
}

std::ostream& Logger::getStdout() {
    return std::cout;
}

LogLevel& Logger::getMinLevelRef() {
    static LogLevel minLevel = LogLevel::Info;
    return minLevel;
}

std::ostream* Logger::getFileStream() {
    static std::ostream* fileStream = nullptr;
    return fileStream;
}

void Logger::setFileStream(std::ostream* os) {
    static std::ostream*& fileStreamRef = *([]() -> std::ostream** {
        static std::ostream* fileStreamInner = nullptr;
        return &fileStreamInner;
        })();
    fileStreamRef = os;
}

void Logger::setLogFile(const std::string& filePath)
{
    std::lock_guard<std::mutex> lock(getMutex());

    // Close previous file, if any
    std::ostream* existing = getFileStream();
    if (existing && existing != &getStdout()) {
        // We only ever allocate std::ofstream here, so safe to delete
        delete existing;
    }

    auto* ofs = new std::ofstream(filePath, std::ios::out | std::ios::app);
    if (!ofs->good()) {
        delete ofs;
        setFileStream(nullptr);
        // Fall back to stdout only
        return;
    }

    setFileStream(ofs);
}

void Logger::setMinLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(getMutex());
    getMinLevelRef() = level;
}

const char* Logger::levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:    return "DEBUG";
    case LogLevel::Info:     return "INFO ";
    case LogLevel::Warn:     return "WARN ";
    case LogLevel::Error:    return "ERROR";
    case LogLevel::Critical: return "CRIT ";
    }
    return "UNKWN";
}

std::string Logger::currentTimestamp()
{
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto time_t = clock::to_time_t(now);

    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &time_t);
#else
    localtime_r(&time_t, &tmBuf);
#endif

    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count() % 1000000;

    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S")
        << "." << std::setfill('0') << std::setw(6) << micros;
    return oss.str();
}

void Logger::log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(getMutex());

    if (level < getMinLevelRef()) {
        return;
    }

    const std::string ts = currentTimestamp();
    const char* lvl = levelToString(level);

    auto& out = getStdout();
    out << "[" << ts << "] [" << lvl << "] " << message << std::endl;

    std::ostream* fileStream = getFileStream();
    if (fileStream && fileStream->good()) {
        (*fileStream) << "[" << ts << "] [" << lvl << "] " << message << std::endl;
    }
}
