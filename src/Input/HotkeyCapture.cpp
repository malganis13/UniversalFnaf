#include "universalfnaf/Input/HotkeyCapture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace universalfnaf {
namespace {

bool IsKeyDown(int virtualKey) noexcept
{
    return (::GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

bool IsFunctionKey(int virtualKey) noexcept
{
    return virtualKey >= VK_F1 && virtualKey <= VK_F24;
}

} // namespace

bool IsAcceptableHotkey(const HotkeyBinding& binding) noexcept
{
    if (!binding.isValid()) {
        return false;
    }
    if ((binding.modifiers & (MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN)) != 0) {
        return true;
    }
    return IsFunctionKey(static_cast<int>(binding.virtualKey));
}

void HotkeyCapture::Begin(HotkeyAction action, bool requireModifier)
{
    active_ = true;
    action_ = action;
    requireModifier_ = requireModifier;
    // Keys already held when the button was clicked must not count as a fresh
    // press, otherwise a stuck mouse button or Ctrl would be captured.
    SnapshotKeyStates();
}

void HotkeyCapture::Cancel()
{
    active_ = false;
}

void HotkeyCapture::SnapshotKeyStates()
{
    previous_.fill(0);
    for (int virtualKey = 8; virtualKey < 256; ++virtualKey) {
        previous_[static_cast<std::size_t>(virtualKey)] = IsKeyDown(virtualKey) ? 1u : 0u;
    }
}

bool HotkeyCapture::IsModifierKey(int virtualKey) noexcept
{
    switch (virtualKey) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
            return true;
        default:
            return false;
    }
}

unsigned HotkeyCapture::CurrentModifiers() noexcept
{
    unsigned modifiers = 0;
    if (IsKeyDown(VK_CONTROL)) { modifiers |= MOD_CONTROL; }
    if (IsKeyDown(VK_SHIFT))   { modifiers |= MOD_SHIFT; }
    if (IsKeyDown(VK_MENU))    { modifiers |= MOD_ALT; }
    if (IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN)) { modifiers |= MOD_WIN; }
    return modifiers;
}

HotkeyCaptureResult HotkeyCapture::Poll(HotkeyBinding& outBinding)
{
    if (!active_) {
        return HotkeyCaptureResult::Idle;
    }

    // Mouse buttons (0x01..0x06) are skipped on purpose: RegisterHotKey cannot
    // register them, and the click that opened the capture must not be caught.
    int pressedKey = 0;
    for (int virtualKey = 8; virtualKey < 256; ++virtualKey) {
        const bool down = IsKeyDown(virtualKey);
        const bool wasDown = previous_[static_cast<std::size_t>(virtualKey)] != 0;
        previous_[static_cast<std::size_t>(virtualKey)] = down ? 1u : 0u;
        if (down && !wasDown && pressedKey == 0 && !IsModifierKey(virtualKey)) {
            pressedKey = virtualKey;
        }
    }

    if (pressedKey == 0) {
        return HotkeyCaptureResult::Idle;
    }
    if (pressedKey == VK_ESCAPE) {
        active_ = false;
        return HotkeyCaptureResult::Cancelled;
    }

    HotkeyBinding candidate;
    candidate.modifiers = CurrentModifiers();
    candidate.virtualKey = static_cast<unsigned>(pressedKey);

    if (requireModifier_ && !IsAcceptableHotkey(candidate)) {
        outBinding = candidate;      // let the UI show what was rejected
        return HotkeyCaptureResult::NeedModifier;
    }

    outBinding = candidate;
    active_ = false;
    return HotkeyCaptureResult::Captured;
}

} // namespace universalfnaf
