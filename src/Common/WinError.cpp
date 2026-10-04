#include "universalfnaf/Common/WinError.h"

#include <cstdio>

namespace universalfnaf {
namespace {

std::wstring DescribeCode(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring text;
    if (length != 0 && buffer != nullptr) {
        text.assign(buffer, length);
        while (!text.empty() &&
               (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) {
            text.pop_back();
        }
    }
    if (buffer != nullptr) {
        ::LocalFree(buffer);
    }
    if (text.empty()) {
        wchar_t fallback[64]{};
        ::swprintf_s(fallback, L"unknown error 0x%08lX", static_cast<unsigned long>(code));
        text.assign(fallback);
    }
    return text;
}

std::string BuildWin32Message(std::string_view what, DWORD code)
{
    std::wstring wide = WideFromUtf8(what);
    wide += L" failed (";
    wchar_t codeText[32]{};
    ::swprintf_s(codeText, L"%lu", static_cast<unsigned long>(code));
    wide += codeText;
    wide += L"): ";
    wide += DescribeCode(code);
    return Utf8FromWide(wide);
}

std::string BuildHresultMessage(std::string_view what, HRESULT hr)
{
    std::wstring wide = WideFromUtf8(what);
    wchar_t codeText[32]{};
    ::swprintf_s(codeText, L"0x%08lX", static_cast<unsigned long>(hr));
    wide += L" failed (HRESULT ";
    wide += codeText;
    wide += L"): ";
    wide += DescribeCode(static_cast<DWORD>(hr));
    return Utf8FromWide(wide);
}

} // namespace

std::string Utf8FromWide(std::wstring_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = ::WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring WideFromUtf8(std::string_view text)
{
    if (text.empty()) {
        return {};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()),
                                           nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                          out.data(), size);
    return out;
}

std::wstring FormatWin32Message(DWORD code)
{
    return DescribeCode(code);
}

std::wstring FormatHresultMessage(long hresult)
{
    return DescribeCode(static_cast<DWORD>(hresult));
}

Win32Error::Win32Error(std::string_view what, DWORD code)
    : std::runtime_error(BuildWin32Message(what, code)), code_(code)
{
}

HresultError::HresultError(std::string_view what, HRESULT hr)
    : std::runtime_error(BuildHresultMessage(what, hr)), hr_(hr)
{
}

} // namespace universalfnaf
