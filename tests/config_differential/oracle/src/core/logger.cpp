#include "pch.h"
#include "logger.h"
#include <cstdio>

namespace RE4HT {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetREFunctions(REFLogFn info, REFLogFn warn, REFLogFn error) {
    m_logInfo = info;
    m_logWarn = warn;
    m_logError = error;
    m_initialized = true;
}

void Logger::LogImpl(REFLogFn fn, const char* fmt, va_list args) {
    if (!m_initialized || !fn) return;
    char buffer[512];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    fn("[RE4HT] %s", buffer);
}

void Logger::Info(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogImpl(m_logInfo, fmt, args);
    va_end(args);
}

void Logger::Warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogImpl(m_logWarn, fmt, args);
    va_end(args);
}

void Logger::Error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogImpl(m_logError, fmt, args);
    va_end(args);
}

} // namespace RE4HT
