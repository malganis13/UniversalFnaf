#include "universalfnaf/Common/Utils.h"

#include "universalfnaf/Common/WinError.h"

#include <shlobj.h>   // SHGetKnownFolderPath / FOLDERID_*

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cstdlib>
#include <vector>

namespace universalfnaf {
namespace {

std::wstring QueryKnownFolder(REFKNOWNFOLDERID folderId)
{
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(::SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &raw)) && raw != nullptr) {
        result.assign(raw);
    }
    if (raw != nullptr) {
        ::CoTaskMemFree(raw);
    }
    return result;
}

} // namespace

std::wstring GetExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written = ::GetModuleFileNameW(nullptr, buffer.data(),
                                                   static_cast<DWORD>(buffer.size()));
        if (written == 0) {
            return {};
        }
        if (static_cast<std::size_t>(written) < buffer.size()) {
            return std::wstring(buffer.data(), written);
        }
        if (buffer.size() >= 32768) {
            return std::wstring(buffer.data(), written);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring GetExecutableDirectory()
{
    return ParentDirectory(GetExecutablePath());
}

std::wstring ParentDirectory(std::wstring_view path)
{
    const std::size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring_view::npos) {
        return {};
    }
    if (pos == 0) {
        return std::wstring(path.substr(0, 1));
    }
    return std::wstring(path.substr(0, pos));
}

std::wstring JoinPath(std::wstring_view directory, std::wstring_view leaf)
{
    if (directory.empty()) {
        return std::wstring(leaf);
    }
    std::wstring result(directory);
    if (result.back() != L'\\' && result.back() != L'/') {
        result.push_back(L'\\');
    }
    result.append(leaf);
    return result;
}

std::wstring GetLocalAppDataDirectory()
{
    std::wstring folder = QueryKnownFolder(FOLDERID_LocalAppData);
    if (folder.empty()) {
        const DWORD needed = ::GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (needed > 1) {
            std::wstring buffer(needed, L'\0');
            const DWORD written = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), needed);
            buffer.resize(written);
            folder = std::move(buffer);
        }
    }
    return folder;
}

bool FileExists(std::wstring_view path)
{
    if (path.empty()) {
        return false;
    }
    const DWORD attributes = ::GetFileAttributesW(std::wstring(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring ToLowerInvariant(std::wstring_view text)
{
    std::wstring result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return result;
}

std::wstring FormatHotkeyText(unsigned modifiers, unsigned virtualKey)
{
    std::wstring text;
    const auto append = [&text](const wchar_t* part) {
        if (!text.empty()) {
            text += L"+";
        }
        text += part;
    };

    if (modifiers & MOD_CONTROL) append(L"Ctrl");
    if (modifiers & MOD_SHIFT)   append(L"Shift");
    if (modifiers & MOD_ALT)     append(L"Alt");
    if (modifiers & MOD_WIN)     append(L"Win");

    if (virtualKey != 0) {
        wchar_t name[64]{};
        UINT scanCode = ::MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
        LONG keyData = static_cast<LONG>(scanCode & 0xFFu) << 16;
        if ((scanCode & 0xE000u) != 0) {
            keyData |= 1L << 24;    // extended key
        }
        if (::GetKeyNameTextW(keyData, name, static_cast<int>(std::size(name))) == 0) {
            ::swprintf_s(name, L"VK 0x%02X", virtualKey);
        }
        append(name);
    }

    if (text.empty()) {
        text = L"<none>";
    }
    return text;
}

void HsvToRgb(float h, float s, float v, float& outR, float& outG, float& outB)
{
    h = h - std::floor(h);                       // wrap into [0,1)
    s = std::clamp(s, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);

    const float sector = h * 6.0f;
    const int index = static_cast<int>(sector) % 6;
    const float fraction = sector - std::floor(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - s * fraction);
    const float t = v * (1.0f - s * (1.0f - fraction));

    switch (index) {
        case 0: outR = v; outG = t; outB = p; break;
        case 1: outR = q; outG = v; outB = p; break;
        case 2: outR = p; outG = v; outB = t; break;
        case 3: outR = p; outG = q; outB = v; break;
        case 4: outR = t; outG = p; outB = v; break;
        default: outR = v; outG = p; outB = q; break;
    }
}

bool IsProcessElevated()
{
    HANDLE rawToken = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &rawToken) == FALSE) {
        return false;
    }
    const auto token = MakeUniqueHandle(rawToken);
    TOKEN_ELEVATION elevation{};
    DWORD returned = 0;
    if (::GetTokenInformation(token.get(), TokenElevation, &elevation,
                              sizeof(elevation), &returned) == FALSE) {
        return false;
    }
    return elevation.TokenIsElevated != 0;
}

} // namespace universalfnaf
