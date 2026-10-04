#include "universalfnaf/Common/Logger.h"

#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

#include <windows.h>

#include <share.h>   // _SH_DENYWR
#include <mutex>

namespace universalfnaf {
namespace {

constexpr unsigned long long kMaxLogBytes = 2ull * 1024ull * 1024ull;

std::string TimestampNow()
{
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    return Sprintf("%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

} // namespace

const wchar_t* ToString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace: return L"TRACE";
        case LogLevel::Debug: return L"DEBUG";
        case LogLevel::Info:  return L"INFO ";
        case LogLevel::Warn:  return L"WARN ";
        case LogLevel::Error: return L"ERROR";
    }
    return L"?    ";
}

std::string Sprintf(const char* format, ...)
{
    char stackBuffer[512];
    va_list args;
    va_start(args, format);
    const int needed = ::vsnprintf(stackBuffer, sizeof(stackBuffer), format, args);
    va_end(args);

    if (needed < 0) {
        return {};
    }
    if (static_cast<std::size_t>(needed) < sizeof(stackBuffer)) {
        return std::string(stackBuffer, static_cast<std::size_t>(needed));
    }

    std::string heap(static_cast<std::size_t>(needed) + 1, '\0');
    va_start(args, format);
    ::vsnprintf(heap.data(), heap.size(), format, args);
    va_end(args);
    heap.resize(static_cast<std::size_t>(needed));
    return heap;
}

FileLogger::FileLogger()
{
    mutex_ = new std::mutex();
}

FileLogger::~FileLogger()
{
    Close();
    delete static_cast<std::mutex*>(mutex_);
    mutex_ = nullptr;
}

bool FileLogger::Open(const std::wstring& filePath, LogLevel minimumLevel)
{
    Close();
    minimumLevel_ = minimumLevel;
    path_ = filePath;

    const std::wstring directory = ParentDirectory(filePath);
    if (!directory.empty()) {
        ::CreateDirectoryW(directory.c_str(), nullptr);   // best effort
    }

    // _SH_DENYWR (not plain fopen): the log stays readable by editors, tail
    // tools and support scripts while the utility is running.
    FILE* file = ::_wfsopen(filePath.c_str(), L"ab", _SH_DENYWR);
    if (file == nullptr) {
        lastError_ = L"Cannot open log file: " + filePath;
        path_.clear();
        return false;
    }
    file_ = file;
    lastError_.clear();

    // Size of the existing file decides whether we must rotate on open.
    long long size = 0;
    if (::_fseeki64(file_, 0, SEEK_END) == 0) {
        size = ::_ftelli64(file_);
    }
    written_ = size > 0 ? static_cast<unsigned long long>(size) : 0;
    RotateIfNeeded();
    return true;
}

void FileLogger::Close()
{
    std::lock_guard<std::mutex> guard(*static_cast<std::mutex*>(mutex_));
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    written_ = 0;
}

void FileLogger::RotateIfNeeded()
{
    if (file_ == nullptr || written_ < kMaxLogBytes) {
        return;
    }
    std::fclose(file_);
    file_ = nullptr;

    const std::wstring backup = path_ + L".1";
    ::DeleteFileW(backup.c_str());
    ::MoveFileW(path_.c_str(), backup.c_str());

    FILE* file = ::_wfsopen(path_.c_str(), L"ab", _SH_DENYWR);
    if (file != nullptr) {
        file_ = file;
        written_ = 0;
    }
}

void FileLogger::Write(LogLevel level, std::string_view component, std::string_view message)
{
    if (level < minimumLevel_) {
        return;
    }

    const std::string line = Sprintf("%s [%-5s] %-10.*s %.*s",
                                     TimestampNow().c_str(),
                                     Utf8FromWide(ToString(level)).c_str(),
                                     static_cast<int>(component.size()), component.data(),
                                     static_cast<int>(message.size()), message.data());

    std::lock_guard<std::mutex> guard(*static_cast<std::mutex*>(mutex_));
    if (file_ != nullptr) {
        std::fwrite(line.data(), 1, line.size(), file_);
        std::fputc('\n', file_);
        std::fflush(file_);                       // crash-safe: no buffered loss
        written_ += line.size() + 1;
        RotateIfNeeded();
    }

    const std::wstring wide = WideFromUtf8(line) + L"\n";
    ::OutputDebugStringW(wide.c_str());
}

void FileLogger::Flush()
{
    std::lock_guard<std::mutex> guard(*static_cast<std::mutex*>(mutex_));
    if (file_ != nullptr) {
        std::fflush(file_);
    }
}

} // namespace universalfnaf
