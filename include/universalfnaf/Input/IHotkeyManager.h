#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Input/IHotkeyManager.h
//  Global hotkeys are registered exclusively through the documented
//  RegisterHotKey / UnregisterHotKey Win32 API. There is no low-level
//  keyboard hook (WH_KEYBOARD_LL) anywhere in this project: the utility never
//  observes keystrokes that are not addressed to it.
// ---------------------------------------------------------------------------

#include <functional>
#include <optional>
#include <string>

// MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN come from the Win32 hotkey API.
// Declaring them here keeps HotkeyBinding self-describing for every consumer,
// including Core/Config, which must not depend on the UI layer.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace universalfnaf {

enum class HotkeyAction {
    ToggleBlocking = 0,
    ToggleClickThrough,
    ToggleOverlay,
    ToggleBorder,
};

const wchar_t* HotkeyActionName(HotkeyAction action) noexcept;

struct HotkeyBinding {
    unsigned modifiers = 0;    // MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN
    unsigned virtualKey = 0;   // VK_* code

    bool isValid() const noexcept { return virtualKey != 0; }
    bool operator==(const HotkeyBinding&) const noexcept = default;
};

class IHotkeyManager {
public:
    using Callback = std::function<void(HotkeyAction)>;

    virtual ~IHotkeyManager() = default;

    // Replaces any previous binding for the action.
    virtual bool Register(HotkeyAction action, const HotkeyBinding& binding, std::wstring& error) = 0;
    virtual bool Unregister(HotkeyAction action, std::wstring& error) = 0;
    virtual void UnregisterAll() = 0;

    // Invoked on the window's owning (UI) thread.
    virtual void SetCallback(Callback callback) = 0;

    virtual std::optional<HotkeyBinding> Binding(HotkeyAction action) const = 0;
};

} // namespace universalfnaf
