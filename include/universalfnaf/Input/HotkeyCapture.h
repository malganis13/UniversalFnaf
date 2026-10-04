#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Input/HotkeyCapture.h
//
//  "Press the combination you want" widget back-end.
//
//  Why polling instead of a keyboard hook: the overlay window is created with
//  WS_EX_NOACTIVATE so it never steals focus. A WM_KEYDOWN-based capture would
//  therefore see nothing. GetAsyncKeyState is the documented, hook-free way to
//  read global key state, and it observes nothing that a normal user could not
//  also observe with the on-screen keyboard. No WH_KEYBOARD / WH_KEYBOARD_LL
//  hook is installed by this project.
// ---------------------------------------------------------------------------

#include "universalfnaf/Input/IHotkeyManager.h"

#include <array>
#include <cstdint>

namespace universalfnaf {

enum class HotkeyCaptureResult {
    Idle,          // nothing captured yet
    Captured,      // outBinding is complete, capture finished
    Cancelled,     // user pressed Escape
    NeedModifier,  // plain key pressed twice in a row: require a modifier
};

class HotkeyCapture {
public:
    void Begin(HotkeyAction action, bool requireModifier = true);
    void Cancel();

    bool active() const noexcept { return active_; }
    HotkeyAction action() const noexcept { return action_; }

    // Call exactly once per UI frame while active().
    HotkeyCaptureResult Poll(HotkeyBinding& outBinding);

private:
    void SnapshotKeyStates();
    static bool IsModifierKey(int virtualKey) noexcept;
    static unsigned CurrentModifiers() noexcept;

    std::array<std::uint8_t, 256> previous_{};
    bool active_ = false;
    bool requireModifier_ = true;
    HotkeyAction action_ = HotkeyAction::ToggleBlocking;
};

// A binding is usable when it has a key and either a modifier or is an F-key.
bool IsAcceptableHotkey(const HotkeyBinding& binding) noexcept;

} // namespace universalfnaf
