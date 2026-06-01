#pragma once

#include <cstdarg>

namespace RE4HT {

// Function pointer type matching REFramework's log_info/log_warn/log_error
using REFLogFn = void (*)(const char* format, ...);

class Logger {
public:
    static Logger& Instance();

    void SetREFunctions(REFLogFn info, REFLogFn warn, REFLogFn error);

    void Info(const char* fmt, ...);
    void Warning(const char* fmt, ...);
    void Error(const char* fmt, ...);

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger() = default;
    ~Logger() = default;

    void LogImpl(REFLogFn fn, const char* fmt, va_list args);

    REFLogFn m_logInfo = nullptr;
    REFLogFn m_logWarn = nullptr;
    REFLogFn m_logError = nullptr;
    bool m_initialized = false;
};

} // namespace RE4HT
