// ---------------------------------------------------------------------------
//  UniversalFnaf :: main.cpp
//
//  Non-invasive outbound traffic filter with a DWM overlay.
//
//  What this process does:
//    * Windows Filtering Platform  - block outbound traffic of one chosen app
//    * RegisterHotKey              - global hotkeys (no keyboard hook)
//    * Dear ImGui + DirectX 11     - overlay rendered into its own swap chain
//    * CreateToolhelp32Snapshot    - process enumeration
//
//  What this process never does:
//    * no OpenProcess with PROCESS_VM_READ / PROCESS_VM_WRITE / VM_OPERATION
//    * no VirtualAllocEx / WriteProcessMemory / CreateRemoteThread
//    * no DLL injection, no SetWindowsHookEx, no API patching (MinHook/Detours)
//    * no kernel driver, no NDIS filter, no WFP callout driver
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>   // CoInitializeEx / CoUninitialize / COINIT_APARTMENTTHREADED

#include "universalfnaf/App/Application.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

#include <exception>
#include <string>

namespace {

constexpr wchar_t kApplicationName[] = L"UniversalFnaf";
constexpr wchar_t kSingleInstanceMutex[] =
    L"Local\\UniversalFnaf.SingleInstance.{5A2E1B74-9C3F-4E8A-B1C7-2F7D4A9E6B31}";

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE /*previousInstance*/,
                      LPWSTR /*commandLine*/, int showCommand)
{
    // COM apartment for IFileOpenDialog (GIF picker). STA: shell dialogs
    // require it and this process has a single UI thread.
    const HRESULT comResult =
        ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const auto comGuard = universalfnaf::MakeScopeGuard([comResult]() noexcept {
        if (SUCCEEDED(comResult)) {
            ::CoUninitialize();
        }
    });

    // Normally applied by the embedded manifest; the call is harmless when the
    // activation context already set the same awareness.
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Single instance per interactive session: two instances would fight over
    // the same dynamic WFP session and the same overlay rectangle.
    const HANDLE rawMutex = ::CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    const auto mutexGuard = universalfnaf::MakeUniqueHandle(rawMutex);
    if (rawMutex != nullptr && ::GetLastError() == ERROR_ALREADY_EXISTS) {
        ::MessageBoxW(nullptr,
                      L"UniversalFnaf is already running.\n\n"
                      L"Use its hotkeys to control it, or close the other instance first.",
                      kApplicationName, MB_ICONINFORMATION | MB_OK);
        return 0;
    }

    try {
        universalfnaf::Application application;
        return application.Run(instance, showCommand);
    } catch (const std::exception& exception) {
        const std::wstring message =
            std::wstring(L"UniversalFnaf could not start:\n\n") +
            universalfnaf::WideFromUtf8(exception.what());
        ::MessageBoxW(nullptr, message.c_str(), kApplicationName, MB_ICONERROR | MB_OK);
        return 1;
    } catch (...) {
        ::MessageBoxW(nullptr, L"UniversalFnaf could not start: unknown error.",
                      kApplicationName, MB_ICONERROR | MB_OK);
        return 1;
    }
}
