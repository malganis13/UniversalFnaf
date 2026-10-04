#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Common/WinError.h
//  Error plumbing shared by every module. Everything is expressed with the
//  documented Win32 / HRESULT error codes so that a log line can be matched
//  against MSDN or `certutil -error` without guesswork.
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace universalfnaf {

// --- string conversion ------------------------------------------------------
std::string  Utf8FromWide(std::wstring_view text);
std::wstring WideFromUtf8(std::string_view text);

// --- error text ------------------------------------------------------------
// FormatMessageW based; falls back to a hex dump when no text is available.
std::wstring FormatWin32Message(DWORD code);
std::wstring FormatHresultMessage(long hresult);

// --- exception types -------------------------------------------------------
class Win32Error final : public std::runtime_error {
public:
    Win32Error(std::string_view what, DWORD code);
    DWORD code() const noexcept { return code_; }
private:
    DWORD code_;
};

class HresultError final : public std::runtime_error {
public:
    HresultError(std::string_view what, HRESULT hr);
    HRESULT hresult() const noexcept { return hr_; }
private:
    HRESULT hr_;
};

// --- assertion helpers ------------------------------------------------------
inline void CheckWin32(BOOL ok, std::string_view what, DWORD code = ::GetLastError())
{
    if (!ok) {
        throw Win32Error(what, code);
    }
}

inline void CheckHandle(HANDLE handle, std::string_view what)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        throw Win32Error(what, ::GetLastError());
    }
}

inline void CheckHr(HRESULT hr, std::string_view what)
{
    if (FAILED(hr)) {
        throw HresultError(what, hr);
    }
}

// --- RAII handle ------------------------------------------------------------
struct HandleCloser {
    void operator()(HANDLE handle) const noexcept
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

inline UniqueHandle MakeUniqueHandle(HANDLE handle) noexcept
{
    return UniqueHandle(handle, HandleCloser{});
}

} // namespace universalfnaf
