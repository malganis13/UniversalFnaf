#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Common/Logger.h
//  Dependency-injected logger (no singletons). A tiny printf-style formatter
//  is provided instead of std::format so the code builds on any C++20
//  toolchain, including ones with an incomplete <format> implementation.
// ---------------------------------------------------------------------------

#include <cstdarg>
#include <cstdio>
#include <string>
#include <string_view>

namespace universalfnaf {

enum class LogLevel { Trace = 0, Debug, Info, Warn, Error };

const wchar_t* ToString(LogLevel level) noexcept;

// Small, allocation-cheap, type-safe-enough formatting helper.
// Usage: log.Info("WFP", Sprintf("filter id=%llu", id));
std::string Sprintf(const char* format, ...);

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void Write(LogLevel level, std::string_view component, std::string_view message) = 0;
    virtual void Flush() = 0;
};

// Thread-safe logger: rotating UTF-8 file under %LOCALAPPDATA%\UniversalFnaf
// plus OutputDebugStringW for debugger / DebugView sessions.
//
// The logger never touches another process, never installs a hook and never
// uses ETW, so it adds exactly zero surface for a security product to trip on.
class FileLogger final : public ILogger {
public:
    FileLogger();
    ~FileLogger() override;

    FileLogger(const FileLogger&) = delete;
    FileLogger& operator=(const FileLogger&) = delete;

    // Opens (or creates) the log file. On failure the logger degrades to
    // debug-output-only mode and reports the reason through lastError().
    bool Open(const std::wstring& filePath, LogLevel minimumLevel = LogLevel::Info);
    void Close();

    void Write(LogLevel level, std::string_view component, std::string_view message) override;
    void Flush() override;

    void SetMinimumLevel(LogLevel level) noexcept { minimumLevel_ = level; }
    LogLevel minimumLevel() const noexcept { return minimumLevel_; }

    bool isFileOpen() const noexcept { return file_ != nullptr; }
    const std::wstring& filePath() const noexcept { return path_; }
    const std::wstring& lastError() const noexcept { return lastError_; }

private:
    void RotateIfNeeded();

    std::FILE* file_ = nullptr;
    std::wstring path_;
    std::wstring lastError_;
    LogLevel minimumLevel_ = LogLevel::Info;
    void* mutex_ = nullptr;      // really a std::mutex*, kept out of the header
    unsigned long long written_ = 0;
};

// --- convenience helpers ----------------------------------------------------
inline void LogTrace(ILogger& log, std::string_view c, std::string_view m) { log.Write(LogLevel::Trace, c, m); }
inline void LogDebug(ILogger& log, std::string_view c, std::string_view m) { log.Write(LogLevel::Debug, c, m); }
inline void LogInfo (ILogger& log, std::string_view c, std::string_view m) { log.Write(LogLevel::Info,  c, m); }
inline void LogWarn (ILogger& log, std::string_view c, std::string_view m) { log.Write(LogLevel::Warn,  c, m); }
inline void LogError(ILogger& log, std::string_view c, std::string_view m) { log.Write(LogLevel::Error, c, m); }

} // namespace universalfnaf
