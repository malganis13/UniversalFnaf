#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Common/Utils.h
//  Small, dependency-free helpers (paths, colour conversion, hotkey captions,
//  scope guards). Nothing here touches another process.
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace universalfnaf {

// --- paths ------------------------------------------------------------------
std::wstring GetExecutablePath();                      // full path of this .exe
std::wstring GetExecutableDirectory();                 // directory of this .exe
std::wstring ParentDirectory(std::wstring_view path);
std::wstring JoinPath(std::wstring_view directory, std::wstring_view leaf);
std::wstring GetLocalAppDataDirectory();               // %LOCALAPPDATA%
bool FileExists(std::wstring_view path);
std::wstring ToLowerInvariant(std::wstring_view text);

// --- text -------------------------------------------------------------------
// "Ctrl+Alt+F8"; falls back to "VK 0x5A" when the key has no printable name.
std::wstring FormatHotkeyText(unsigned modifiers, unsigned virtualKey);

// --- colour -----------------------------------------------------------------
// h in [0,1) or any real (it wraps), s/v in [0,1].
void HsvToRgb(float h, float s, float v, float& outR, float& outG, float& outB);

// --- process environment ----------------------------------------------------
bool IsProcessElevated();

// --- RAII helper ------------------------------------------------------------
template <typename Fn>
class ScopeGuard {
public:
    static_assert(std::is_invocable_v<Fn&>, "ScopeGuard requires a nullary callable");

    explicit ScopeGuard(Fn fn) noexcept : fn_(std::move(fn)) {}
    ~ScopeGuard() noexcept
    {
        if (active_) {
            fn_();
        }
    }

    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;

    ScopeGuard(ScopeGuard&& other) noexcept
        : fn_(std::move(other.fn_)), active_(other.active_)
    {
        other.active_ = false;
    }
    ScopeGuard& operator=(ScopeGuard&&) = delete;

    void Dismiss() noexcept { active_ = false; }

private:
    Fn fn_;
    bool active_ = true;
};

template <typename Fn>
[[nodiscard]] ScopeGuard<Fn> MakeScopeGuard(Fn fn) noexcept
{
    return ScopeGuard<Fn>(std::move(fn));
}

} // namespace universalfnaf
