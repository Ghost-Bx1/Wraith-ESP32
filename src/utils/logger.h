#pragma once
#include <Arduino.h>
#include <vector>
#include <functional>
#include "../config.h"

enum LogLevel { LOG_INFO, LOG_WARN, LOG_ERROR, LOG_SUCCESS, LOG_DATA };

struct LogEntry {
    unsigned long ts;
    LogLevel level;
    String msg;
};

class Logger {
public:
    static Logger& get() { static Logger inst; return inst; }

    LogLevel serialLevel = LOG_WARN; // apenas WARN/ERROR/SUCCESS/DATA no serial

    void log(LogLevel lvl, const String& msg) {
        LogEntry e{ millis(), lvl, msg };
        _buf.push_back(e);
        if (_buf.size() > MAX_LOG_LINES) _buf.erase(_buf.begin());
        for (auto& cb : _cbs) cb(e);
        if (lvl >= serialLevel)
            Serial.printf("[%lu] %s\n", e.ts, msg.c_str());
    }

    void info   (const String& m) { log(LOG_INFO,    m); }
    void warn   (const String& m) { log(LOG_WARN,    m); }
    void error  (const String& m) { log(LOG_ERROR,   m); }
    void success(const String& m) { log(LOG_SUCCESS, m); }
    void data   (const String& m) { log(LOG_DATA,    m); }

    const std::vector<LogEntry>& entries() const { return _buf; }
    void clear() { _buf.clear(); }

    void onEntry(std::function<void(const LogEntry&)> cb) { _cbs.push_back(cb); }

private:
    std::vector<LogEntry> _buf;
    std::vector<std::function<void(const LogEntry&)>> _cbs;
};

#define LOG Logger::get()
