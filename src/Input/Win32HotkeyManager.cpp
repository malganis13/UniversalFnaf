#include "universalfnaf/Input/Win32HotkeyManager.h"

#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

namespace universalfnaf {
namespace {

constexpr wchar_t kHotkeyWindowClass[] = L"UniversalFnaf.HotkeySink";
constexpr wchar_t kHotkeyWindowTitle[] = L"UniversalFnaf hotkey sink";

} // namespace

const wchar_t* HotkeyActionName(HotkeyAction action) noexcept
{
    switch (action) {
        case HotkeyAction::ToggleBlocking:     return L"Block traffic";
        case HotkeyAction::ToggleClickThrough: return L"Click-through";
        case HotkeyAction::ToggleOverlay:      return L"Show overlay";
        case HotkeyAction::ToggleBorder:       return L"RGB border";
    }
    return L"Unknown";
}

Win32HotkeyManager::Win32HotkeyManager(ILogger& logger) : logger_(logger)
{
    instance_ = ::GetModuleHandleW(nullptr);
}

Win32HotkeyManager::~Win32HotkeyManager()
{
    UnregisterAll();
    DestroyMessageWindow();
}

bool Win32HotkeyManager::EnsureMessageWindow(std::wstring& error)
{
    if (window_ != nullptr) {
        return true;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = &Win32HotkeyManager::StaticWindowProc;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = kHotkeyWindowClass;
    windowClass.cbWndExtra = sizeof(void*);

    if (::RegisterClassExW(&windowClass) == 0 &&
        ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        error = L"RegisterClassExW (hotkey sink) failed: " + FormatWin32Message(::GetLastError());
        return false;
    }

    // WS_POPUP + never shown: the window exists purely as a message sink for
    // WM_HOTKEY. It is deliberately not HWND_MESSAGE so that hotkey delivery
    // works on every supported Windows build.
    window_ = ::CreateWindowExW(0, kHotkeyWindowClass, kHotkeyWindowTitle, WS_POPUP,
                                0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (window_ == nullptr) {
        error = L"CreateWindowExW (hotkey sink) failed: " + FormatWin32Message(::GetLastError());
        return false;
    }
    return true;
}

void Win32HotkeyManager::DestroyMessageWindow()
{
    if (window_ != nullptr) {
        ::DestroyWindow(window_);
        window_ = nullptr;
    }
    ::UnregisterClassW(kHotkeyWindowClass, instance_);
}

LRESULT CALLBACK Win32HotkeyManager::StaticWindowProc(HWND window, UINT message,
                                                      WPARAM wParam, LPARAM lParam)
{
    Win32HotkeyManager* self =
        reinterpret_cast<Win32HotkeyManager*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<Win32HotkeyManager*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->WindowProc(window, message, wParam, lParam);
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Win32HotkeyManager::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_HOTKEY) {
        const int hotkeyId = static_cast<int>(wParam);
        for (const auto& [action, entry] : entries_) {
            if (entry.hotkeyId == hotkeyId) {
                LogInfo(logger_, "HOTKEY", Sprintf("triggered: %s", Utf8FromWide(HotkeyActionName(action)).c_str()));
                if (callback_) {
                    callback_(action);
                }
                return 0;
            }
        }
        return 0;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

bool Win32HotkeyManager::Register(HotkeyAction action, const HotkeyBinding& binding,
                                  std::wstring& error)
{
    error.clear();
    if (!binding.isValid()) {
        error = L"Hotkey has no virtual key assigned";
        return false;
    }

    std::wstring windowError;
    if (!EnsureMessageWindow(windowError)) {
        error = windowError;
        return false;
    }

    std::wstring ignored;
    Unregister(action, ignored);

    const int hotkeyId = nextHotkeyId_++;
    const UINT flags = binding.modifiers | MOD_NOREPEAT;

    if (::RegisterHotKey(window_, hotkeyId, flags, binding.virtualKey) == FALSE) {
        const DWORD lastError = ::GetLastError();
        error = L"RegisterHotKey failed for ";
        error += FormatHotkeyText(binding.modifiers, binding.virtualKey);
        error += L": ";
        error += FormatWin32Message(lastError);
        if (lastError == ERROR_HOTKEY_ALREADY_REGISTERED) {
            error += L" (the combination is already owned by another program)";
        }
        LogWarn(logger_, "HOTKEY", Utf8FromWide(error));
        return false;
    }

    entries_[action] = Entry{hotkeyId, binding};
    LogInfo(logger_, "HOTKEY", Sprintf("registered %s for action '%s'",
                                       Utf8FromWide(FormatHotkeyText(binding.modifiers, binding.virtualKey)).c_str(),
                                       Utf8FromWide(HotkeyActionName(action)).c_str()));
    return true;
}

bool Win32HotkeyManager::Unregister(HotkeyAction action, std::wstring& error)
{
    error.clear();
    const auto iterator = entries_.find(action);
    if (iterator == entries_.end()) {
        return true;
    }
    if (window_ != nullptr) {
        ::UnregisterHotKey(window_, iterator->second.hotkeyId);
    }
    entries_.erase(iterator);
    return true;
}

void Win32HotkeyManager::UnregisterAll()
{
    for (const auto& [action, entry] : entries_) {
        (void)action;
        if (window_ != nullptr) {
            ::UnregisterHotKey(window_, entry.hotkeyId);
        }
    }
    entries_.clear();
}

void Win32HotkeyManager::SetCallback(Callback callback)
{
    callback_ = std::move(callback);
}

std::optional<HotkeyBinding> Win32HotkeyManager::Binding(HotkeyAction action) const
{
    const auto iterator = entries_.find(action);
    if (iterator == entries_.end()) {
        return std::nullopt;
    }
    return iterator->second.binding;
}

} // namespace universalfnaf
