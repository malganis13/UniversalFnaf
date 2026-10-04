#include "universalfnaf/UI/OverlayWindow.h"

#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

#include <algorithm>

namespace universalfnaf {
namespace {

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

constexpr LONG_PTR kClickThroughStyle = WS_EX_TRANSPARENT;

} // namespace

OverlayWindow::OverlayWindow(ILogger& logger) : logger_(logger)
{
    instance_ = ::GetModuleHandleW(nullptr);
}

OverlayWindow::~OverlayWindow()
{
    Destroy();
}

RECT OverlayWindow::PrimaryMonitorBounds()
{
    RECT bounds{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    POINT origin{0, 0};
    const HMONITOR monitor = ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &info) != FALSE) {
        bounds = info.rcMonitor;
    }
    return bounds;
}

RECT OverlayWindow::PrimaryWorkArea()
{
    RECT work = PrimaryMonitorBounds();
    POINT origin{0, 0};
    const HMONITOR monitor = ::MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor != nullptr && ::GetMonitorInfoW(monitor, &info) != FALSE) {
        work = info.rcWork;   // excludes the taskbar
    }
    return work;
}

RECT OverlayWindow::VirtualScreenBounds()
{
    RECT bounds{};
    bounds.left = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    bounds.top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    bounds.right = bounds.left + ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    bounds.bottom = bounds.top + ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return bounds;
}

bool OverlayWindow::Create(const OverlayWindowOptions& options, std::wstring& error)
{
    error.clear();
    if (window_ != nullptr) {
        return true;
    }

    options_ = options;
    clickThrough_ = options.clickThrough;
    topmost_ = options.topmost;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &OverlayWindow::StaticWindowProc;
    windowClass.hInstance = instance_;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;   // the swap chain paints everything
    windowClass.lpszClassName = options_.className;
    windowClass.cbWndExtra = sizeof(void*);

    if (::RegisterClassExW(&windowClass) == 0) {
        if (::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            error = L"RegisterClassExW failed: " + FormatWin32Message(::GetLastError());
            return false;
        }
    } else {
        classRegistered_ = true;
    }

    DWORD extendedStyle = 0;
    if (options_.topmost)      { extendedStyle |= WS_EX_TOPMOST; }
    if (options_.noActivate)   { extendedStyle |= WS_EX_NOACTIVATE; }
    if (options_.showInTaskbar) { extendedStyle |= WS_EX_APPWINDOW; }
    if (options_.clickThrough) { extendedStyle |= kClickThroughStyle; }

    if (options_.directComposition) {
        extendedStyle |= WS_EX_NOREDIRECTIONBITMAP;
    } else {
        extendedStyle |= WS_EX_LAYERED;
    }

    window_ = ::CreateWindowExW(extendedStyle, options_.className, options_.title, WS_POPUP,
                                options_.x, options_.y, options_.width, options_.height,
                                nullptr, nullptr, instance_, this);
    if (window_ == nullptr) {
        error = L"CreateWindowExW failed: " + FormatWin32Message(::GetLastError());
        return false;
    }

    if (!options_.directComposition && !ApplyColorKey()) {
        error = L"SetLayeredWindowAttributes(LWA_COLORKEY) failed: " +
                FormatWin32Message(::GetLastError());
        Destroy();
        return false;
    }

    SetExcludeFromCapture(options_.excludeFromCapture);
    EnsureOnScreen();

    LogInfo(logger_, "OVERLAY", Sprintf("window created %dx%d at (%d,%d) mode=%s",
                                        options_.width, options_.height,
                                        options_.x, options_.y,
                                        options_.directComposition ? "DirectComposition"
                                                                   : "colour-key"));
    return true;
}

void OverlayWindow::Destroy()
{
    if (window_ != nullptr) {
        ::SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        ::DestroyWindow(window_);
        window_ = nullptr;
    }
    if (classRegistered_) {
        ::UnregisterClassW(options_.className, instance_);
        classRegistered_ = false;
    }
    visible_ = false;
}

bool OverlayWindow::ApplyColorKey()
{
    if (window_ == nullptr) {
        return false;
    }
    return ::SetLayeredWindowAttributes(window_, static_cast<COLORREF>(options_.colorKey),
                                        0, LWA_COLORKEY) != FALSE;
}

LRESULT CALLBACK OverlayWindow::StaticWindowProc(HWND window, UINT message,
                                                 WPARAM wParam, LPARAM lParam)
{
    OverlayWindow* self =
        reinterpret_cast<OverlayWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        self = static_cast<OverlayWindow*>(create->lpCreateParams);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self != nullptr) {
        return self->WindowProc(window, message, wParam, lParam);
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

LRESULT OverlayWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    // The application (and the ImGui Win32 backend) get first refusal on every
    // message; whatever they do not consume falls through to our handling.
    if (messageHook_) {
        if (const std::optional<LRESULT> result = messageHook_(window, message, wParam, lParam)) {
            return *result;
        }
    }

    switch (message) {
        case WM_ERASEBKGND:
            return 1;   // DWM/DirectComposition composites our surface

        case WM_NCHITTEST:
            if (clickThrough_) {
                return HTTRANSPARENT;
            }
            return HTCLIENT;   // the whole client area is interactive (UI-drawn controls)

        case WM_MOUSEACTIVATE:
            if (options_.noActivate) {
                return MA_NOACTIVATE;   // game mode: never steal focus
            }
            break;

        case WM_CLOSE:
            ::DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }

    return ::DefWindowProcW(window, message, wParam, lParam);
}

void OverlayWindow::ApplyExtendedStyles()
{
    if (window_ == nullptr) {
        return;
    }
    LONG_PTR style = ::GetWindowLongPtrW(window_, GWL_EXSTYLE);

    style = clickThrough_ ? (style | kClickThroughStyle) : (style & ~kClickThroughStyle);

    if (options_.noActivate) {
        style |= WS_EX_NOACTIVATE;
    } else {
        style &= ~WS_EX_NOACTIVATE;
    }

    if (options_.showInTaskbar) {
        style |= WS_EX_APPWINDOW;
        style &= ~WS_EX_TOOLWINDOW;
    } else {
        style &= ~WS_EX_APPWINDOW;
        style |= WS_EX_TOOLWINDOW;
    }

    ::SetWindowLongPtrW(window_, GWL_EXSTYLE, style);
    ::SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void OverlayWindow::SetClickThrough(bool enabled)
{
    if (clickThrough_ == enabled) {
        return;
    }
    clickThrough_ = enabled;
    ApplyExtendedStyles();
    LogInfo(logger_, "OVERLAY", enabled ? "click-through ENABLED (mouse passes to the window below)"
                                        : "click-through DISABLED (mouse interacts with this window)");
}

void OverlayWindow::SetVisible(bool visible)
{
    if (window_ == nullptr) {
        return;
    }
    visible_ = visible;
    // SW_SHOWNOACTIVATE: toggling visibility with a hotkey must not steal focus.
    ::ShowWindow(window_, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    if (visible && topmost_) {
        ::SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    LogInfo(logger_, "OVERLAY", visible ? "window shown" : "window hidden");
}

void OverlayWindow::SetTopmost(bool topmost)
{
    if (window_ == nullptr) {
        return;
    }
    topmost_ = topmost;
    ::SetWindowPos(window_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void OverlayWindow::SetNoActivate(bool noActivate)
{
    if (options_.noActivate == noActivate) {
        return;
    }
    options_.noActivate = noActivate;
    ApplyExtendedStyles();
    LogInfo(logger_, "OVERLAY", noActivate
                                    ? "game mode: the window will not take focus"
                                    : "normal mode: the window may take focus");
}

void OverlayWindow::SetShowInTaskbar(bool show)
{
    if (options_.showInTaskbar == show) {
        return;
    }
    options_.showInTaskbar = show;
    ApplyExtendedStyles();
}

bool OverlayWindow::SetExcludeFromCapture(bool exclude)
{
    if (window_ == nullptr) {
        return false;
    }
    const BOOL ok = ::SetWindowDisplayAffinity(window_,
                                               exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
    excludeFromCapture_ = ok != FALSE && exclude;
    if (exclude && ok == FALSE) {
        LogWarn(logger_, "OVERLAY",
                Sprintf("SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) failed: %ls",
                        FormatWin32Message(::GetLastError()).c_str()));
    }
    return exclude ? (ok != FALSE) : true;
}

RECT OverlayWindow::bounds() const
{
    RECT result{0, 0, options_.width, options_.height};
    if (window_ != nullptr) {
        ::GetWindowRect(window_, &result);
    }
    return result;
}

SIZE OverlayWindow::clientSize() const
{
    SIZE size{options_.width, options_.height};
    if (window_ != nullptr) {
        RECT client{};
        if (::GetClientRect(window_, &client) != FALSE) {
            size.cx = client.right - client.left;
            size.cy = client.bottom - client.top;
        }
    }
    return size;
}

int OverlayWindow::width() const
{
    return clientSize().cx;
}

int OverlayWindow::height() const
{
    return clientSize().cy;
}

void OverlayWindow::MoveBy(int deltaX, int deltaY)
{
    if (window_ == nullptr || (deltaX == 0 && deltaY == 0)) {
        return;
    }
    const RECT current = bounds();
    ::SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                   current.left + deltaX, current.top + deltaY, 0, 0,
                   SWP_NOSIZE | SWP_NOACTIVATE);
    options_.x = current.left + deltaX;
    options_.y = current.top + deltaY;
}

void OverlayWindow::ResizeBy(int deltaX, int deltaY, int minimumWidth, int minimumHeight)
{
    if (window_ == nullptr || (deltaX == 0 && deltaY == 0)) {
        return;
    }
    const RECT current = bounds();
    // RECT members are LONG; cast once so the template overloads resolve.
    const int currentWidth = static_cast<int>(current.right - current.left);
    const int currentHeight = static_cast<int>(current.bottom - current.top);
    const int newWidth = std::max(minimumWidth, currentWidth + deltaX);
    const int newHeight = std::max(minimumHeight, currentHeight + deltaY);
    ::SetWindowPos(window_, nullptr, 0, 0, newWidth, newHeight,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    options_.width = newWidth;
    options_.height = newHeight;
}

bool OverlayWindow::SetBounds(int x, int y, int width, int height)
{
    if (window_ == nullptr) {
        return false;
    }
    options_.x = x;
    options_.y = y;
    options_.width = width;
    options_.height = height;
    return ::SetWindowPos(window_, topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, width, height,
                          SWP_NOACTIVATE) != FALSE;
}

void OverlayWindow::EnsureOnScreen()
{
    if (window_ == nullptr) {
        return;
    }

    RECT current = bounds();
    const HMONITOR monitor = ::MonitorFromRect(&current, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || ::GetMonitorInfoW(monitor, &info) == FALSE) {
        return;
    }

    const RECT work = info.rcWork;
    const int workWidth = static_cast<int>(work.right - work.left);
    const int workHeight = static_cast<int>(work.bottom - work.top);
    const int currentLeft = static_cast<int>(current.left);
    const int currentTop = static_cast<int>(current.top);
    const int width = static_cast<int>(current.right - current.left);
    const int height = static_cast<int>(current.bottom - current.top);

    // Keep the whole window inside a work area; a window restored on a monitor
    // that no longer exists must never become unreachable.
    const int x = std::clamp(currentLeft, static_cast<int>(work.left),
                             std::max(static_cast<int>(work.left), workWidth - width +
                                                                     static_cast<int>(work.left)));
    const int y = std::clamp(currentTop, static_cast<int>(work.top),
                             std::max(static_cast<int>(work.top), workHeight - height +
                                                                     static_cast<int>(work.top)));
    if (x == currentLeft && y == currentTop) {
        return;
    }

    LogInfo(logger_, "OVERLAY", Sprintf("window clamped into the work area: (%d,%d)", x, y));
    SetBounds(x, y, width, height);
}

void OverlayWindow::ResetPosition()
{
    if (window_ == nullptr) {
        return;
    }
    const RECT work = PrimaryWorkArea();
    const int workWidth = static_cast<int>(work.right - work.left);
    const int workHeight = static_cast<int>(work.bottom - work.top);
    const int width = std::min(options_.width, workWidth);
    const int height = std::min(options_.height, workHeight);
    SetBounds(static_cast<int>(work.left) + 40, static_cast<int>(work.top) + 40, width, height);
}

} // namespace universalfnaf
