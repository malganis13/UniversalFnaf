#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: App/Application.h
//
//  Composition root. Owns every subsystem, wires the dependencies explicitly
//  (no globals, no singletons) and runs the message/render loop.
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "universalfnaf/Input/IHotkeyManager.h"
#include "universalfnaf/Net/INetworkFilter.h"
#include "universalfnaf/Process/IProcessMonitor.h"
#include "universalfnaf/UI/UIManager.h"

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace universalfnaf {

class FileLogger;
class GdiPlusSession;
class ConfigManager;
class OverlayWindow;
class Renderer;
class GifAnimator;
class Win32ProcessManager;
class WfpFilterManager;
class Win32HotkeyManager;
class Win32TcpConnectionTerminator;

class Application {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Throws Win32Error / std::runtime_error on unrecoverable startup failure.
    int Run(HINSTANCE instance, int showCommand);

private:
    using Clock = std::chrono::steady_clock;

    bool Initialize(HINSTANCE instance, std::wstring& error);
    void Shutdown();
    void MainLoop();

    void InstallWindowMessageHook();

    void ProcessPendingHotkeys();
    void OnHotkey(HotkeyAction action);

    void RefreshProcesses(bool force);
    void ApplyProcessSelection();

    void ApplyGifIfNeeded(bool force);

    void LoadConfigIntoUiState();
    void SaveUiStateToConfig(const UiState& state);

    void RegisterHotkeysFromConfig();
    bool RebindHotkey(HotkeyAction action, const HotkeyBinding& binding, std::wstring& error);

    bool ResolveBlockTarget(BlockTarget& target, std::wstring& error);
    void ToggleBlocking();

    void ApplyRequests(const UiRequests& requests, UiState& state);
    void UpdateUiState(UiState& state, double deltaSeconds);
    void EnsureWindowOnScreen();
    void StoreWindowGeometry();
    void TerminateTargetConnections(const BlockTarget& target);

    void SetStatus(std::wstring message, bool isError);
    void SetStatusOk(std::wstring message) { SetStatus(std::move(message), false); }
    void SetStatusError(std::wstring message) { SetStatus(std::move(message), true); }

    std::unique_ptr<FileLogger> logger_;
    std::unique_ptr<GdiPlusSession> gdiPlus_;
    std::unique_ptr<ConfigManager> config_;
    std::unique_ptr<OverlayWindow> overlay_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<GifAnimator> gifAnimator_;
    std::unique_ptr<UIManager> uiManager_;
    std::unique_ptr<Win32HotkeyManager> hotkeyManager_;
    std::unique_ptr<Win32ProcessManager> processMonitor_;
    std::unique_ptr<WfpFilterManager> networkFilter_;
    std::unique_ptr<Win32TcpConnectionTerminator> tcpTerminator_;

    UiState uiState_;

    std::deque<HotkeyAction> pendingHotkeys_;
    std::mutex pendingHotkeysMutex_;

    std::wstring statusLine_;
    bool statusIsError_ = false;
    std::wstring hotkeyError_;
    std::wstring processListError_;
    std::wstring gifError_;
    std::wstring loadedGifPath_;

    Clock::time_point startTime_{};
    Clock::time_point lastFrameTime_{};
    Clock::time_point lastProcessRefresh_{};
    double framesPerSecond_ = 0.0;
    double fpsAccumulator_ = 0.0;
    int fpsFrames_ = 0;
    double processListAgeSeconds_ = 0.0;
    double refreshIntervalSeconds_ = 1.0;

    // Absolute-cursor window drag / resize. ImGui mouse coordinates are relative
    // to this window, so a per-frame delta would feed window movement back into
    // itself; anchoring to GetCursorPos() removes that feedback loop.
    bool windowDragging_ = false;
    POINT windowDragCursor_{};
    RECT windowDragBounds_{};
    bool windowResizing_ = false;
    POINT windowResizeCursor_{};
    RECT windowResizeBounds_{};

    HINSTANCE instance_ = nullptr;
    bool running_ = false;
    bool initialized_ = false;
};

} // namespace universalfnaf
