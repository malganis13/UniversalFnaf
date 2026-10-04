#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Input/Win32HotkeyManager.h
//
//  Owns a hidden, never-shown top-level window whose only purpose is to
//  receive WM_HOTKEY messages. The window is created with no visible style and
//  is never activated, so registering a hotkey cannot steal focus from the
//  program the user is actually working in.
// ---------------------------------------------------------------------------

#include "universalfnaf/Input/IHotkeyManager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <map>

namespace universalfnaf {

class ILogger;

class Win32HotkeyManager final : public IHotkeyManager {
public:
    explicit Win32HotkeyManager(ILogger& logger);
    ~Win32HotkeyManager() override;

    Win32HotkeyManager(const Win32HotkeyManager&) = delete;
    Win32HotkeyManager& operator=(const Win32HotkeyManager&) = delete;

    bool Register(HotkeyAction action, const HotkeyBinding& binding, std::wstring& error) override;
    bool Unregister(HotkeyAction action, std::wstring& error) override;
    void UnregisterAll() override;

    void SetCallback(Callback callback) override;

    std::optional<HotkeyBinding> Binding(HotkeyAction action) const override;

    HWND messageWindow() const noexcept { return window_; }

private:
    struct Entry {
        int hotkeyId = 0;
        HotkeyBinding binding;
    };

    bool EnsureMessageWindow(std::wstring& error);
    void DestroyMessageWindow();

    static LRESULT CALLBACK StaticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    ILogger& logger_;
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    int nextHotkeyId_ = 0x4000;
    std::map<HotkeyAction, Entry> entries_;
    Callback callback_;
};

} // namespace universalfnaf
