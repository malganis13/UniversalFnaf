#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: UI/OverlayWindow.h
//
//  A normal, small, user-movable application window - not a full-screen layer:
//    * WS_POPUP, no system frame; the frame is drawn by the UI (themed, with
//      the RGB border hugging the window itself)
//    * moved by dragging the header, resized by dragging the bottom-right grip
//    * activatable by default, so text fields and file dialogs work normally
//    * WS_EX_TOPMOST (optional) - stays above other windows while gaming
//    * WS_EX_TRANSPARENT (optional) - click-through for game mode
//    * WS_EX_NOREDIRECTIONBITMAP + DirectComposition, or WS_EX_LAYERED +
//      LWA_COLORKEY as the fallback transparency path
//
//  Because the window only covers its own rectangle, the rest of the desktop
//  stays fully usable and the window can be dragged to any monitor.
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace universalfnaf {

class ILogger;

struct OverlayWindowOptions {
    // true  -> composed through DirectComposition (no WS_EX_LAYERED)
    // false -> classic WS_EX_LAYERED + LWA_COLORKEY window
    bool directComposition = true;

    bool topmost = true;
    bool noActivate = false;        // game mode: never steal focus
    bool showInTaskbar = true;      // WS_EX_APPWINDOW: findable like any window
    bool clickThrough = false;
    bool excludeFromCapture = false;

    std::uint32_t colorKey = 0x00000000u;   // colour-key mode only

    int x = 80;
    int y = 80;
    int width = 620;
    int height = 800;

    const wchar_t* className = L"UniversalFnaf.Window";
    const wchar_t* title = L"UniversalFnaf";
};

class OverlayWindow {
public:
    // Return a value to consume the message, or nullopt to keep processing.
    using MessageHook = std::function<std::optional<LRESULT>(HWND, UINT, WPARAM, LPARAM)>;

    explicit OverlayWindow(ILogger& logger);
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool Create(const OverlayWindowOptions& options, std::wstring& error);
    void Destroy();

    HWND handle() const noexcept { return window_; }
    bool valid() const noexcept { return window_ != nullptr; }
    const OverlayWindowOptions& options() const noexcept { return options_; }

    void SetMessageHook(MessageHook hook) { messageHook_ = std::move(hook); }

    void SetClickThrough(bool enabled);
    bool clickThrough() const noexcept { return clickThrough_; }

    void SetVisible(bool visible);
    bool visible() const noexcept { return visible_; }

    void SetTopmost(bool topmost);
    bool topmost() const noexcept { return topmost_; }

    // WS_EX_NOACTIVATE: true means the window never takes keyboard focus
    // ("game mode"). Off by default so that text fields work normally.
    void SetNoActivate(bool noActivate);
    bool noActivate() const noexcept { return options_.noActivate; }

    void SetShowInTaskbar(bool show);
    bool showInTaskbar() const noexcept { return options_.showInTaskbar; }

    // SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) - keeps the window out
    // of screenshots and screen recordings. Documented, opt-in.
    bool SetExcludeFromCapture(bool exclude);

    bool ApplyColorKey();

    // --- geometry -----------------------------------------------------------
    void MoveBy(int deltaX, int deltaY);
    void ResizeBy(int deltaX, int deltaY, int minimumWidth = 460, int minimumHeight = 360);
    bool SetBounds(int x, int y, int width, int height);
    bool MoveTo(int x, int y, int width, int height) { return SetBounds(x, y, width, height); }

    RECT bounds() const;                 // window rect in screen coordinates
    SIZE clientSize() const;             // client area in pixels
    int width() const;                   // client width
    int height() const;                  // client height

    // Keeps the window inside a visible work area (after a monitor change or a
    // DPI change) and marks the rectangle as reachable by the user.
    void EnsureOnScreen();
    // Default placement: left edge of the primary monitor's work area.
    void ResetPosition();

    static RECT PrimaryMonitorBounds();
    static RECT PrimaryWorkArea();
    static RECT VirtualScreenBounds();

private:
    static LRESULT CALLBACK StaticWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void ApplyExtendedStyles();

    ILogger& logger_;
    OverlayWindowOptions options_{};
    HWND window_ = nullptr;
    HINSTANCE instance_ = nullptr;
    bool clickThrough_ = false;
    bool visible_ = false;
    bool topmost_ = true;
    bool excludeFromCapture_ = false;
    bool classRegistered_ = false;
    MessageHook messageHook_;
};

} // namespace universalfnaf
