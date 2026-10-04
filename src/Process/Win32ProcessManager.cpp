#include "universalfnaf/Process/Win32ProcessManager.h"

#include "universalfnaf/Common/GdiPlusSession.h"
#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

#include <tlhelp32.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace universalfnaf {
namespace {

// Service / OS plumbing that a user never wants to block: blocking these
// breaks networking, the shell, audio or Defender itself.
const wchar_t* const kSystemProcessNames[] = {
    L"system",              L"system idle process",   L"registry",
    L"memory compression",  L"secure system",         L"idle",
    L"smss.exe",            L"csrss.exe",             L"wininit.exe",
    L"winlogon.exe",        L"services.exe",          L"lsass.exe",
    L"svchost.exe",         L"fontdrvhost.exe",       L"dwm.exe",
    L"spoolsv.exe",         L"conhost.exe",           L"sihost.exe",
    L"taskhostw.exe",       L"runtimebroker.exe",     L"searchindexer.exe",
    L"searchhost.exe",      L"startmenuexperiencehost.exe",
    L"shellexperiencehost.exe", L"textinputhost.exe", L"ctfmon.exe",
    L"explorer.exe",        L"audiodg.exe",           L"dllhost.exe",
    L"wudfhost.exe",        L"securityhealthservice.exe", L"msmpeng.exe",
    L"nissrv.exe",          L"trustedinstaller.exe",  L"tiworker.exe",
    L"mousocoreworker.exe", L"wsmprovhost.exe",       L"sppsvc.exe",
    L"wlanext.exe",         L"applicationframehost.exe", L"widgets.exe",
    L"lockapp.exe",         L"wmiprvse.exe",
};

void CollectWindowedProcessIds(std::unordered_set<std::uint32_t>& out)
{
    struct Context {
        std::unordered_set<std::uint32_t>* sink;
    } context{&out};

    ::EnumWindows(
        [](HWND window, LPARAM param) -> BOOL {
            auto* ctx = reinterpret_cast<Context*>(param);
            if (::IsWindowVisible(window) == FALSE) {
                return TRUE;
            }
            if (::GetWindow(window, GW_OWNER) != nullptr) {
                return TRUE;   // owned popups/tool windows do not count
            }
            DWORD pid = 0;
            ::GetWindowThreadProcessId(window, &pid);
            if (pid != 0) {
                ctx->sink->insert(pid);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&context));
}

} // namespace

Win32ProcessManager::Win32ProcessManager(ILogger& logger,
                                         const GdiPlusSession& gdiPlus,
                                         Options options)
    : logger_(logger), gdiPlus_(gdiPlus), options_(options)
{
}

bool Win32ProcessManager::IsSystemProcessName(const std::wstring& lowerCaseImageName)
{
    for (const wchar_t* name : kSystemProcessNames) {
        if (lowerCaseImageName == name) {
            return true;
        }
    }
    return false;
}

bool Win32ProcessManager::ResolveImagePath(std::uint32_t processId,
                                           std::wstring& imagePath,
                                           std::wstring& error)
{
    imagePath.clear();
    error.clear();

    // PROCESS_QUERY_LIMITED_INFORMATION: metadata only. This is the *minimal*
    // right that allows QueryFullProcessImageNameW. No memory access is
    // possible with it, which is exactly the guarantee this project promises.
    HANDLE rawHandle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (rawHandle == nullptr) {
        error = FormatWin32Message(::GetLastError());
        return false;
    }
    const auto process = MakeUniqueHandle(rawHandle);

    std::vector<wchar_t> buffer(1024);
    for (;;) {
        DWORD size = static_cast<DWORD>(buffer.size());
        if (::QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &size) != FALSE) {
            imagePath.assign(buffer.data(), size);
            return true;
        }
        const DWORD lastError = ::GetLastError();
        if (lastError == ERROR_INSUFFICIENT_BUFFER && buffer.size() < 32768) {
            buffer.resize(buffer.size() * 2);
            continue;
        }
        error = FormatWin32Message(lastError);
        return false;
    }
}

bool Win32ProcessManager::IsProcessElevatedByHandle(void* processHandle)
{
    HANDLE rawToken = nullptr;
    if (::OpenProcessToken(reinterpret_cast<HANDLE>(processHandle), TOKEN_QUERY, &rawToken) == FALSE) {
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

const Win32ProcessManager::IconData* Win32ProcessManager::LookupOrLoadIcon(const std::wstring& imagePath)
{
    if (imagePath.empty()) {
        return nullptr;
    }

    const auto cached = iconCache_.find(imagePath);
    if (cached != iconCache_.end()) {
        return cached->second.valid ? &cached->second : nullptr;
    }

    IconData data;
    SHFILEINFOW shellInfo{};
    const DWORD_PTR result = ::SHGetFileInfoW(imagePath.c_str(), FILE_ATTRIBUTE_NORMAL, &shellInfo,
                                              sizeof(shellInfo), SHGFI_ICON | SHGFI_SMALLICON);
    if (result != 0 && shellInfo.hIcon != nullptr) {
        int width = 0;
        int height = 0;
        if (GdiPlusSession::IconToRgba(shellInfo.hIcon, data.rgba, width, height)) {
            data.width = width;
            data.height = height;
            data.valid = true;
        }
        ::DestroyIcon(shellInfo.hIcon);   // SHELL32 hands ownership to us
    }

    auto [iterator, inserted] = iconCache_.emplace(imagePath, std::move(data));
    (void)inserted;
    return iterator->second.valid ? &iterator->second : nullptr;
}

bool Win32ProcessManager::EnsureIcon(ProcessInfo& info)
{
    if (!info.iconRgba.empty()) {
        return true;
    }
    const IconData* data = LookupOrLoadIcon(info.imagePath);
    if (data == nullptr) {
        return false;
    }
    info.iconRgba = data->rgba;
    info.iconWidth = data->width;
    info.iconHeight = data->height;
    return true;
}

std::vector<ProcessInfo> Win32ProcessManager::Snapshot(std::wstring& error)
{
    error.clear();
    std::vector<ProcessInfo> result;

    const HANDLE rawSnapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (rawSnapshot == INVALID_HANDLE_VALUE) {
        error = L"CreateToolhelp32Snapshot failed: " + FormatWin32Message(::GetLastError());
        return result;
    }
    const auto snapshot = MakeUniqueHandle(rawSnapshot);

    std::unordered_set<std::uint32_t> windowedPids;
    if (options_.requireVisibleWindow) {
        CollectWindowedProcessIds(windowedPids);
    }

    const std::uint32_t selfPid = ::GetCurrentProcessId();
    // DWORD (unsigned long), not uint32_t (unsigned int): ProcessIdToSessionId
    // takes a DWORD*, and the two 32-bit unsigned types are distinct in MSVC.
    DWORD selfSession = 0;
    ::ProcessIdToSessionId(selfPid, &selfSession);

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot.get(), &entry) == FALSE) {
        const DWORD lastError = ::GetLastError();
        if (lastError != ERROR_NO_MORE_FILES) {
            error = L"Process32FirstW failed: " + FormatWin32Message(lastError);
        }
        return result;
    }

    do {
        if (entry.th32ProcessID == 0 || entry.th32ProcessID == selfPid) {
            continue;   // PID 0 is the idle task; never target ourselves
        }
        if (result.size() >= options_.maxProcesses) {
            LogWarn(logger_, "PROC", Sprintf("process list truncated at %zu entries",
                                             options_.maxProcesses));
            break;
        }

        ProcessInfo info;
        info.processId = entry.th32ProcessID;
        info.parentProcessId = entry.th32ParentProcessID;
        info.imageName = entry.szExeFile;

        const std::wstring lowerName = ToLowerInvariant(info.imageName);
        if (!options_.includeSystemProcesses && IsSystemProcessName(lowerName)) {
            continue;
        }

        DWORD session = 0;
        if (::ProcessIdToSessionId(info.processId, &session) != FALSE) {
            info.sessionId = session;
        }
        if (options_.onlyCurrentSession && session != selfSession) {
            continue;
        }
        if (options_.requireVisibleWindow && windowedPids.count(info.processId) == 0) {
            continue;
        }

        std::wstring resolveError;
        if (!ResolveImagePath(info.processId, info.imagePath, resolveError)) {
            if (options_.requireImagePath) {
                continue;
            }
        }
        if (info.imagePath.empty()) {
            continue;
        }

        info.hasVisibleWindow = windowedPids.count(info.processId) != 0;
        info.isCurrentProcess = false;

        // Metadata-only query; used purely for a UI hint.
        HANDLE rawProcess = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, info.processId);
        if (rawProcess != nullptr) {
            info.isElevated = IsProcessElevatedByHandle(rawProcess);
            ::CloseHandle(rawProcess);
        }

        if (options_.resolveIcons) {
            EnsureIcon(info);
        }

        result.push_back(std::move(info));
    } while (::Process32NextW(snapshot.get(), &entry) != FALSE);

    std::sort(result.begin(), result.end(), [](const ProcessInfo& left, const ProcessInfo& right) {
        const int nameOrder = _wcsicmp(left.imageName.c_str(), right.imageName.c_str());
        if (nameOrder != 0) {
            return nameOrder < 0;
        }
        return left.processId < right.processId;
    });

    LogDebug(logger_, "PROC", Sprintf("snapshot: %zu user processes", result.size()));
    return result;
}

} // namespace universalfnaf
