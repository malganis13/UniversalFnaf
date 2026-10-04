#include "universalfnaf/App/Application.h"

#include "universalfnaf/Common/GdiPlusSession.h"
#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"
#include "universalfnaf/Core/ConfigManager.h"
#include "universalfnaf/Input/Win32HotkeyManager.h"
#include "universalfnaf/Net/WfpFilterManager.h"
#include "universalfnaf/Net/Win32TcpConnectionTerminator.h"
#include "universalfnaf/Process/Win32ProcessManager.h"
#include "universalfnaf/UI/GifAnimator.h"
#include "universalfnaf/UI/OverlayWindow.h"
#include "universalfnaf/UI/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace universalfnaf {
namespace {

constexpr wchar_t kApplicationName[] = L"UniversalFnaf";
constexpr int kTargetFpsWithoutVsync = 60;

std::wstring LogFilePath()
{
    const std::wstring directory = JoinPath(GetLocalAppDataDirectory(), L"UniversalFnaf");
    if (!directory.empty()) {
        ::CreateDirectoryW(directory.c_str(), nullptr);
    }
    return JoinPath(directory, L"universalfnaf.log");
}

LogLevel LevelFromInt(int value)
{
    return static_cast<LogLevel>(std::clamp(value, 0, 4));
}

} // namespace

Application::Application() = default;

Application::~Application()
{
    Shutdown();
}

// ---------------------------------------------------------------------------
//  Startup
// ---------------------------------------------------------------------------

bool Application::Initialize(HINSTANCE instance, std::wstring& error)
{
    instance_ = instance;

    // --- logging ------------------------------------------------------------
    logger_ = std::make_unique<FileLogger>();
    if (!logger_->Open(LogFilePath(), LogLevel::Info)) {
        // The utility must still run without a log file.
        ::OutputDebugStringW(L"UniversalFnaf: logging to file is unavailable\n");
    }
    LogInfo(*logger_, "APP", "=== UniversalFnaf starting ===");
    LogInfo(*logger_, "APP", Sprintf("elevated=%s executable=%s",
                                     IsProcessElevated() ? "yes" : "no",
                                     Utf8FromWide(GetExecutablePath()).c_str()));

    if (!IsProcessElevated()) {
        LogWarn(*logger_, "APP", "not running elevated: WFP filtering will be unavailable");
    }

    // --- GDI+ (GIF decoding + process icons) --------------------------------
    gdiPlus_ = std::make_unique<GdiPlusSession>();
    if (!gdiPlus_->ok()) {
        error = L"GDI+ could not be initialized";
        return false;
    }

    // --- configuration ------------------------------------------------------
    config_ = std::make_unique<ConfigManager>(*logger_, ConfigManager::DefaultPath());
    std::wstring configError;
    if (!config_->Load(configError)) {
        SetStatusError(L"config.json could not be parsed, defaults are in use: " + configError);
    }
    logger_->SetMinimumLevel(LevelFromInt(config_->config().logLevel));
    refreshIntervalSeconds_ = static_cast<double>(config_->config().refreshIntervalMs) / 1000.0;

    // --- process monitor ----------------------------------------------------
    Win32ProcessManager::Options processOptions;
    processOptions.includeSystemProcesses = config_->config().showSystemProcesses;
    processOptions.onlyCurrentSession = true;
    processOptions.requireImagePath = true;
    processOptions.resolveIcons = true;
    processMonitor_ = std::make_unique<Win32ProcessManager>(*logger_, *gdiPlus_, processOptions);

    // --- WFP ----------------------------------------------------------------
    networkFilter_ = std::make_unique<WfpFilterManager>(*logger_);
    std::wstring wfpError;
    if (!networkFilter_->Initialize(wfpError)) {
        SetStatusError(L"WFP unavailable: " + wfpError);
    } else {
        SetStatusOk(L"Ready. Select a process and press the block hotkey.");
    }
    tcpTerminator_ = std::make_unique<Win32TcpConnectionTerminator>(*logger_);

    // --- application window -------------------------------------------------
    // A normal, small, movable window: it covers only its own rectangle, so the
    // rest of the desktop stays usable and it can be dragged to any monitor.
    OverlayWindowOptions windowOptions;
    windowOptions.directComposition =
        config_->config().transparencyMode == TransparencyMode::DirectComposition;
    windowOptions.topmost = config_->config().topmost;
    windowOptions.noActivate = config_->config().noActivate;
    windowOptions.showInTaskbar = config_->config().showInTaskbar;
    windowOptions.clickThrough = config_->config().clickThrough;
    windowOptions.excludeFromCapture = config_->config().excludeFromCapture;
    windowOptions.x = config_->config().windowX;
    windowOptions.y = config_->config().windowY;
    windowOptions.width = config_->config().windowWidth;
    windowOptions.height = config_->config().windowHeight;

    overlay_ = std::make_unique<OverlayWindow>(*logger_);
    std::wstring windowError;
    if (!overlay_->Create(windowOptions, windowError)) {
        error = windowError;
        return false;
    }

    // --- renderer -----------------------------------------------------------
    RendererOptions rendererOptions;
    rendererOptions.mode = config_->config().transparencyMode;
    rendererOptions.vsync = true;

    renderer_ = std::make_unique<Renderer>(*logger_);
    std::wstring rendererError;
    if (!renderer_->Initialize(overlay_->handle(), overlay_->width(), overlay_->height(),
                               rendererOptions, rendererError)) {
        if (rendererOptions.mode == TransparencyMode::DirectComposition) {
            // Documented fallback: layered window + colour key needs no
            // DirectComposition support (older VMs, some remote sessions).
            LogWarn(*logger_, "APP", "DirectComposition unavailable, retrying with colour-key mode");
            windowOptions.directComposition = false;
            overlay_->Destroy();
            if (!overlay_->Create(windowOptions, windowError)) {
                error = windowError;
                return false;
            }
            rendererOptions.mode = TransparencyMode::ColorKeyLayered;
            if (!renderer_->Initialize(overlay_->handle(), overlay_->width(), overlay_->height(),
                                       rendererOptions, rendererError)) {
                error = rendererError;
                return false;
            }
            SetStatusError(L"DirectComposition failed, using the colour-key fallback");
        } else {
            error = rendererError;
            return false;
        }
    }

    // --- GIF ----------------------------------------------------------------
    gifAnimator_ = std::make_unique<GifAnimator>(*gdiPlus_, *logger_);

    // --- UI -----------------------------------------------------------------
    uiManager_ = std::make_unique<UIManager>(*logger_, *renderer_, *gifAnimator_);
    std::wstring uiError;
    if (!uiManager_->Initialize(overlay_->handle(), uiError)) {
        error = uiError;
        return false;
    }
    InstallWindowMessageHook();

    // --- hotkeys ------------------------------------------------------------
    hotkeyManager_ = std::make_unique<Win32HotkeyManager>(*logger_);
    hotkeyManager_->SetCallback([this](HotkeyAction action) {
        std::lock_guard<std::mutex> guard(pendingHotkeysMutex_);
        pendingHotkeys_.push_back(action);
    });
    RegisterHotkeysFromConfig();

    // --- initial state ------------------------------------------------------
    LoadConfigIntoUiState();
    RefreshProcesses(true);

    // The overlay always starts visible: a hidden overlay would look like a
    // process that does nothing. Hiding it is one hotkey press away.
    overlay_->SetVisible(true);
    uiState_.overlayVisible = true;

    if (config_->config().blockOnStartup && !config_->config().targetImagePath.empty()) {
        ToggleBlocking();
    }

    startTime_ = Clock::now();
    lastFrameTime_ = startTime_;
    lastProcessRefresh_ = startTime_;
    initialized_ = true;
    LogInfo(*logger_, "APP", "startup completed");
    return true;
}

void Application::InstallWindowMessageHook()
{
    overlay_->SetMessageHook([this](HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) -> std::optional<LRESULT> {
        if (uiManager_ != nullptr) {
            if (const std::optional<LRESULT> handled =
                    uiManager_->ProcessWindowMessage(window, message, wParam, lParam)) {
                return handled;
            }
        }

        switch (message) {
            case WM_SIZE: {
                const int width = LOWORD(lParam);
                const int height = HIWORD(lParam);
                if (renderer_ != nullptr && renderer_->initialized() && width > 0 && height > 0 &&
                    (width != renderer_->width() || height != renderer_->height())) {
                    uiManager_->InvalidateDeviceObjects();
                    std::wstring resizeError;
                    if (!renderer_->Resize(width, height, resizeError)) {
                        SetStatusError(L"Resize failed: " + resizeError);
                        running_ = false;
                    }
                    uiManager_->RecreateDeviceObjects();
                }
                return 0;
            }

            case WM_DISPLAYCHANGE:
            case WM_DPICHANGED:
            case WM_SETTINGCHANGE:
                if (initialized_) {
                    if (message == WM_DPICHANGED && uiManager_ != nullptr) {
                        uiManager_->SetDpiScale(static_cast<float>(::GetDpiForWindow(window)) / 96.0f);
                    }
                    EnsureWindowOnScreen();
                }
                return std::nullopt;

            default:
                break;
        }
        return std::nullopt;
    });
}

void Application::EnsureWindowOnScreen()
{
    if (overlay_ == nullptr || !overlay_->valid()) {
        return;
    }
    overlay_->EnsureOnScreen();
    StoreWindowGeometry();

    const SIZE client = overlay_->clientSize();
    if (renderer_ != nullptr && renderer_->initialized() && uiManager_ != nullptr &&
        client.cx > 0 && client.cy > 0 &&
        (client.cx != renderer_->width() || client.cy != renderer_->height())) {
        uiManager_->InvalidateDeviceObjects();
        std::wstring resizeError;
        if (!renderer_->Resize(client.cx, client.cy, resizeError)) {
            SetStatusError(L"Resize failed: " + resizeError);
            running_ = false;
        }
        uiManager_->RecreateDeviceObjects();
    }
}

void Application::StoreWindowGeometry()
{
    if (overlay_ == nullptr || config_ == nullptr || !overlay_->valid()) {
        return;
    }
    const RECT bounds = overlay_->bounds();
    AppConfig& config = config_->config();
    config.windowX = bounds.left;
    config.windowY = bounds.top;
    config.windowWidth = bounds.right - bounds.left;
    config.windowHeight = bounds.bottom - bounds.top;
}

void Application::TerminateTargetConnections(const BlockTarget& target)
{
    if (tcpTerminator_ == nullptr) {
        return;
    }
    if (target.processId == 0) {
        SetStatusError(L"Cannot terminate connections: the target PID is unknown. "
                       L"Re-select the process in the list.");
        return;
    }

    const TcpTerminationResult result = tcpTerminator_->TerminateOutboundForProcess(target.processId);
    const UiStrings& text = StringsFor(config_->config().language);
    const std::string message = Sprintf(text.terminateResult, result.terminated,
                                        result.inboundPreserved, result.failed);
    if (result.terminated > 0) {
        SetStatusOk(WideFromUtf8(message));
    } else {
        SetStatusError(WideFromUtf8(message));
    }
}

void Application::Shutdown()
{
    // Remember where the user put the window before the objects disappear.
    StoreWindowGeometry();

    if (networkFilter_ != nullptr) {
        if (config_ != nullptr && config_->config().removeRulesOnExit &&
            networkFilter_->IsBlocking()) {
            std::wstring removeError;
            if (!networkFilter_->RemoveOutboundBlock(removeError)) {
                LogWarn(*logger_, "APP", Utf8FromWide(removeError));
            }
        }
        networkFilter_->Shutdown();   // dynamic session: the OS drops everything else
    }

    if (hotkeyManager_ != nullptr) {
        hotkeyManager_->UnregisterAll();
    }
    if (uiManager_ != nullptr) {
        uiManager_->Shutdown();
    }
    uiManager_.reset();
    gifAnimator_.reset();
    renderer_.reset();
    overlay_.reset();
    hotkeyManager_.reset();
    processMonitor_.reset();
    networkFilter_.reset();
    tcpTerminator_.reset();

    if (logger_ != nullptr) {
        LogInfo(*logger_, "APP", "=== UniversalFnaf stopped ===");
        logger_->Flush();
    }
    config_.reset();
    gdiPlus_.reset();
    logger_.reset();
    initialized_ = false;
}

// ---------------------------------------------------------------------------
//  Main loop
// ---------------------------------------------------------------------------

int Application::Run(HINSTANCE instance, int showCommand)
{
    (void)showCommand;

    std::wstring error;
    if (!Initialize(instance, error)) {
        if (logger_ != nullptr) {
            LogError(*logger_, "APP", Utf8FromWide(error));
        }
        MessageBoxW(nullptr, error.c_str(), kApplicationName, MB_ICONERROR | MB_OK);
        Shutdown();
        return 2;
    }

    MainLoop();
    Shutdown();
    return 0;
}

void Application::MainLoop()
{
    running_ = true;
    MSG message{};

    while (running_) {
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            if (message.message == WM_QUIT) {
                running_ = false;
                break;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (!running_) {
            break;
        }

        const Clock::time_point now = Clock::now();
        double deltaSeconds = std::chrono::duration<double>(now - lastFrameTime_).count();
        lastFrameTime_ = now;
        // Clamp pathological deltas (debugger break, display power cycle).
        deltaSeconds = std::clamp(deltaSeconds, 0.0, 0.25);

        ProcessPendingHotkeys();
        RefreshProcesses(false);
        ApplyGifIfNeeded(false);

        gifAnimator_->Update(deltaSeconds);

        UpdateUiState(uiState_, deltaSeconds);

        UiRequests requests;
        const bool frameStarted = renderer_->BeginFrame();
        if (!frameStarted) {
            SetStatusError(L"The DirectX 11 render target is unavailable. Restart the utility.");
            running_ = false;
        } else {
            uiManager_->NewFrame();
            uiManager_->RenderFrame(uiState_, requests, deltaSeconds);
            const bool presented = renderer_->EndFrame();
            if (!presented) {
                SetStatusError(L"The DirectX 11 device was lost. Restart the utility.");
                running_ = false;
            }
        }

        ApplyRequests(requests, uiState_);

        if (!renderer_->options().vsync) {
            // Frame pacing when vsync is disabled by configuration.
            const double frameSeconds = 1.0 / static_cast<double>(kTargetFpsWithoutVsync);
            const double elapsed = std::chrono::duration<double>(Clock::now() - now).count();
            if (elapsed < frameSeconds) {
                ::Sleep(static_cast<DWORD>((frameSeconds - elapsed) * 1000.0));
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  Hotkeys
// ---------------------------------------------------------------------------

void Application::ProcessPendingHotkeys()
{
    std::deque<HotkeyAction> pending;
    {
        std::lock_guard<std::mutex> guard(pendingHotkeysMutex_);
        pending.swap(pendingHotkeys_);
    }
    for (const HotkeyAction action : pending) {
        OnHotkey(action);
    }
}

void Application::OnHotkey(HotkeyAction action)
{
    switch (action) {
        case HotkeyAction::ToggleBlocking:
            ToggleBlocking();
            break;

        case HotkeyAction::ToggleClickThrough:
            uiState_.clickThrough = !uiState_.clickThrough;
            overlay_->SetClickThrough(uiState_.clickThrough);
            config_->config().clickThrough = uiState_.clickThrough;
            SetStatusOk(uiState_.clickThrough ? L"Click-through enabled (mouse passes through)"
                                              : L"Click-through disabled");
            break;

        case HotkeyAction::ToggleOverlay:
            uiState_.overlayVisible = !uiState_.overlayVisible;
            overlay_->SetVisible(uiState_.overlayVisible);
            config_->config().overlayVisible = uiState_.overlayVisible;
            break;

        case HotkeyAction::ToggleBorder:
            uiState_.borderEnabled = !uiState_.borderEnabled;
            config_->config().borderEnabled = uiState_.borderEnabled;
            break;
    }
}

void Application::RegisterHotkeysFromConfig()
{
    const AppConfig& config = config_->config();
    hotkeyError_.clear();

    struct Item {
        HotkeyAction action;
        const HotkeyBinding* binding;
    };
    const Item items[] = {
        {HotkeyAction::ToggleBlocking, &config.toggleBlocking},
        {HotkeyAction::ToggleClickThrough, &config.toggleClickThrough},
        {HotkeyAction::ToggleOverlay, &config.toggleOverlay},
        {HotkeyAction::ToggleBorder, &config.toggleBorder},
    };

    for (const Item& item : items) {
        std::wstring registerError;
        if (!hotkeyManager_->Register(item.action, *item.binding, registerError)) {
            if (!hotkeyError_.empty()) {
                hotkeyError_ += L"\n";
            }
            hotkeyError_ += registerError;
        }
    }

    if (!hotkeyError_.empty()) {
        SetStatusError(hotkeyError_);
    }
}

bool Application::RebindHotkey(HotkeyAction action, const HotkeyBinding& binding,
                               std::wstring& error)
{
    error.clear();

    HotkeyBinding previous;
    if (const std::optional<HotkeyBinding> current = hotkeyManager_->Binding(action)) {
        previous = *current;
    }

    if (!hotkeyManager_->Register(action, binding, error)) {
        // Restore the previous binding so a failed rebind never leaves the
        // action without a hotkey.
        if (previous.isValid()) {
            std::wstring ignored;
            hotkeyManager_->Register(action, previous, ignored);
        }
        return false;
    }

    AppConfig& config = config_->config();
    switch (action) {
        case HotkeyAction::ToggleBlocking:     config.toggleBlocking = binding; break;
        case HotkeyAction::ToggleClickThrough: config.toggleClickThrough = binding; break;
        case HotkeyAction::ToggleOverlay:      config.toggleOverlay = binding; break;
        case HotkeyAction::ToggleBorder:       config.toggleBorder = binding; break;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  Processes
// ---------------------------------------------------------------------------

void Application::RefreshProcesses(bool force)
{
    const Clock::time_point now = Clock::now();
    const double age = std::chrono::duration<double>(now - lastProcessRefresh_).count();
    processListAgeSeconds_ = age;
    if (!force && age < refreshIntervalSeconds_) {
        return;
    }
    lastProcessRefresh_ = now;
    processListAgeSeconds_ = 0.0;

    std::wstring error;
    std::vector<ProcessInfo> snapshot = processMonitor_->Snapshot(error);
    if (!error.empty()) {
        processListError_ = error;
        LogWarn(*logger_, "APP", Utf8FromWide(error));
        return;
    }
    processListError_.clear();

    // Preserve the selection across refreshes (path first, then PID).
    std::wstring selectedPath;
    std::uint32_t selectedPid = 0;
    if (uiState_.selectedProcessIndex >= 0 &&
        static_cast<std::size_t>(uiState_.selectedProcessIndex) < uiState_.processes.size()) {
        const ProcessInfo& current = uiState_.processes[static_cast<std::size_t>(
            uiState_.selectedProcessIndex)];
        selectedPath = current.imagePath;
        selectedPid = current.processId;
    }
    if (selectedPath.empty() && !config_->config().targetImagePath.empty()) {
        selectedPath = config_->config().targetImagePath;
        selectedPid = config_->config().targetProcessId;
    }

    uiState_.processes = std::move(snapshot);
    uiState_.selectedProcessIndex = -1;

    for (std::size_t index = 0; index < uiState_.processes.size(); ++index) {
        const ProcessInfo& info = uiState_.processes[index];
        if ((!selectedPath.empty() && _wcsicmp(info.imagePath.c_str(), selectedPath.c_str()) == 0) ||
            (selectedPid != 0 && info.processId == selectedPid)) {
            uiState_.selectedProcessIndex = static_cast<int>(index);
            break;
        }
    }
}

void Application::ApplyProcessSelection()
{
    if (uiState_.selectedProcessIndex < 0 ||
        static_cast<std::size_t>(uiState_.selectedProcessIndex) >= uiState_.processes.size()) {
        return;
    }
    const ProcessInfo& info =
        uiState_.processes[static_cast<std::size_t>(uiState_.selectedProcessIndex)];

    AppConfig& config = config_->config();
    config.targetProcessId = info.processId;
    config.targetImageName = info.imageName;
    config.targetImagePath = info.imagePath;

    // Switching the target while a block is active re-applies it immediately.
    if (networkFilter_->IsBlocking()) {
        ToggleBlocking();   // release the old target
        ToggleBlocking();   // engage the new one
    }
}

// ---------------------------------------------------------------------------
//  GIF
// ---------------------------------------------------------------------------

void Application::ApplyGifIfNeeded(bool force)
{
    const std::wstring& desired = uiState_.gifPath;
    if (!force && desired == loadedGifPath_) {
        return;
    }

    loadedGifPath_ = desired;
    gifError_.clear();

    if (desired.empty()) {
        gifAnimator_->Clear();
        uiState_.gifLoaded = false;
        return;
    }

    std::wstring loadError;
    if (gifAnimator_->Load(desired, loadError)) {
        uiState_.gifLoaded = true;
        uiState_.gifWidth = gifAnimator_->width();
        uiState_.gifHeight = gifAnimator_->height();
        uiState_.gifFrameCount = gifAnimator_->frameCount();
    } else {
        gifError_ = loadError;
        uiState_.gifLoaded = false;
        LogWarn(*logger_, "APP", Utf8FromWide(loadError));
    }
}

// ---------------------------------------------------------------------------
//  Configuration <-> UI state
// ---------------------------------------------------------------------------

void Application::LoadConfigIntoUiState()
{
    const AppConfig& config = config_->config();

    uiState_.toggleBlocking = config.toggleBlocking;
    uiState_.toggleClickThrough = config.toggleClickThrough;
    uiState_.toggleOverlay = config.toggleOverlay;
    uiState_.toggleBorder = config.toggleBorder;

    uiState_.clickThrough = config.clickThrough;
    uiState_.topmost = config.topmost;
    uiState_.excludeFromCapture = config.excludeFromCapture;
    uiState_.overlayVisible = config.overlayVisible;
    uiState_.noActivate = config.noActivate;
    uiState_.showInTaskbar = config.showInTaskbar;
    uiState_.language = config.language;
    uiState_.borderGlow = config.borderGlow;
    uiState_.blockOnStartup = config.blockOnStartup;
    uiState_.terminateExistingConnections = config.terminateExistingConnections;

    uiState_.borderEnabled = config.borderEnabled;
    uiState_.borderThickness = config.borderThickness;
    uiState_.hueSpeed = config.hueSpeed;
    uiState_.borderSaturation = config.borderSaturation;
    uiState_.borderValue = config.borderValue;

    uiState_.gifEnabled = config.gifEnabled;
    uiState_.gifScale = config.gifScale;
    uiState_.gifPath = config.gifPath;
    loadedGifPath_.clear();
    ApplyGifIfNeeded(true);

    uiState_.showSystemProcesses = config.showSystemProcesses;

    overlay_->SetClickThrough(config.clickThrough);
    overlay_->SetTopmost(config.topmost);
    overlay_->SetNoActivate(config.noActivate);
    overlay_->SetShowInTaskbar(config.showInTaskbar);
    overlay_->SetExcludeFromCapture(config.excludeFromCapture);
    overlay_->SetBounds(config.windowX, config.windowY, config.windowWidth, config.windowHeight);
    overlay_->EnsureOnScreen();
    StoreWindowGeometry();
}

void Application::SaveUiStateToConfig(const UiState& state)
{
    AppConfig& config = config_->config();

    config.toggleBlocking = state.toggleBlocking;
    config.toggleClickThrough = state.toggleClickThrough;
    config.toggleOverlay = state.toggleOverlay;
    config.toggleBorder = state.toggleBorder;

    config.clickThrough = state.clickThrough;
    config.topmost = state.topmost;
    config.excludeFromCapture = state.excludeFromCapture;
    config.overlayVisible = state.overlayVisible;
    config.noActivate = state.noActivate;
    config.showInTaskbar = state.showInTaskbar;
    config.language = state.language;
    config.blockOnStartup = state.blockOnStartup;
    config.terminateExistingConnections = state.terminateExistingConnections;

    config.borderEnabled = state.borderEnabled;
    config.borderThickness = state.borderThickness;
    config.hueSpeed = state.hueSpeed;
    config.borderSaturation = state.borderSaturation;
    config.borderValue = state.borderValue;
    config.borderGlow = state.borderGlow;

    config.gifEnabled = state.gifEnabled;
    config.gifScale = state.gifScale;
    config.gifPath = state.gifPath;

    config.showSystemProcesses = state.showSystemProcesses;
    config.refreshIntervalMs = static_cast<int>(refreshIntervalSeconds_ * 1000.0);

    // Window placement travels with the settings.
    StoreWindowGeometry();
}

// ---------------------------------------------------------------------------
//  Blocking
// ---------------------------------------------------------------------------

bool Application::ResolveBlockTarget(BlockTarget& target, std::wstring& error)
{
    error.clear();
    AppConfig& config = config_->config();

    std::uint32_t processId = config.targetProcessId;
    std::wstring imagePath = config.targetImagePath;
    std::wstring imageName = config.targetImageName;

    if (uiState_.selectedProcessIndex >= 0 &&
        static_cast<std::size_t>(uiState_.selectedProcessIndex) < uiState_.processes.size()) {
        const ProcessInfo& info =
            uiState_.processes[static_cast<std::size_t>(uiState_.selectedProcessIndex)];
        processId = info.processId;
        imagePath = info.imagePath;
        imageName = info.imageName;
    }

    if (imagePath.empty() && processId != 0) {
        std::wstring resolveError;
        if (processMonitor_->ResolveImagePath(processId, imagePath, resolveError)) {
            const std::size_t separator = imagePath.find_last_of(L"\\/");
            imageName = separator == std::wstring::npos ? imagePath : imagePath.substr(separator + 1);
        } else {
            error = L"The selected process has exited and its image path is unknown (" +
                    resolveError + L")";
            return false;
        }
    }

    if (imagePath.empty()) {
        error = L"Select a target process first";
        return false;
    }

    target.processId = processId;
    target.imagePath = std::move(imagePath);
    target.imageName = std::move(imageName);
    return true;
}

void Application::ToggleBlocking()
{
    if (!networkFilter_->IsReady()) {
        SetStatusError(L"WFP is unavailable. Run the utility as administrator. " +
                       networkFilter_->Status());
        return;
    }

    if (networkFilter_->IsBlocking()) {
        std::wstring error;
        if (networkFilter_->RemoveOutboundBlock(error)) {
            SetStatusOk(L"Outbound traffic allowed again");
        } else {
            SetStatusError(error);
        }
        return;
    }

    BlockTarget target;
    std::wstring error;
    if (!ResolveBlockTarget(target, error)) {
        SetStatusError(error);
        return;
    }

    if (!networkFilter_->ApplyOutboundBlock(target, error)) {
        SetStatusError(error);
        return;
    }

    // Remember the target so the next session can re-arm quickly.
    AppConfig& config = config_->config();
    config.targetProcessId = target.processId;
    config.targetImageName = target.imageName;
    config.targetImagePath = target.imagePath;

    SetStatusOk(L"Blocking outbound traffic for " + target.imageName);

    if (config.terminateExistingConnections) {
        TerminateTargetConnections(target);
    }
}

// ---------------------------------------------------------------------------
//  UI plumbing
// ---------------------------------------------------------------------------

void Application::UpdateUiState(UiState& state, double deltaSeconds)
{
    state.engineReady = networkFilter_ != nullptr && networkFilter_->IsReady();
    state.engineStatus = networkFilter_ != nullptr ? networkFilter_->Status() : L"not initialized";
    state.blocking = networkFilter_ != nullptr && networkFilter_->IsBlocking();

    if (networkFilter_ != nullptr) {
        const BlockTarget target = networkFilter_->CurrentTarget();
        state.targetName = target.imageName;
        state.targetPid = target.processId;
        state.targetPath = target.imagePath;
        state.activeRuleCount = networkFilter_->ActiveRules().size();
        if (state.targetName.empty()) {
            state.targetName = config_->config().targetImageName;
            state.targetPid = config_->config().targetProcessId;
            state.targetPath = config_->config().targetImagePath;
        }
    }

    if (!state.blocking && state.selectedProcessIndex >= 0 &&
        static_cast<std::size_t>(state.selectedProcessIndex) < state.processes.size()) {
        // While idle the panel shows what *would* be blocked: the drop-down
        // selection, which takes precedence over the remembered target.
        const ProcessInfo& selected =
            state.processes[static_cast<std::size_t>(state.selectedProcessIndex)];
        state.targetName = selected.imageName;
        state.targetPid = selected.processId;
        state.targetPath = selected.imagePath;
    }

    state.statusLine = statusLine_;
    state.statusIsError = statusIsError_;
    state.language = config_->config().language;
    state.configPath = config_->path();
    state.logPath = logger_->filePath();
    state.hotkeyError = hotkeyError_;
    state.processListError = processListError_;
    state.processListAgeSeconds = processListAgeSeconds_;
    state.gifError = gifError_;
    state.versionLine = L"v1.0";
    state.uptimeSeconds = std::chrono::duration<double>(Clock::now() - startTime_).count();

    ++fpsFrames_;
    fpsAccumulator_ += deltaSeconds;
    if (fpsAccumulator_ >= 0.5) {
        framesPerSecond_ = static_cast<double>(fpsFrames_) / fpsAccumulator_;
        fpsFrames_ = 0;
        fpsAccumulator_ = 0.0;
    }
    state.framesPerSecond = framesPerSecond_;
}

void Application::ApplyRequests(const UiRequests& requests, UiState& state)
{
    if (requests.quit) {
        LogInfo(*logger_, "APP", "exit requested from the UI");
        running_ = false;
    }

    if (requests.toggleBlocking) {
        ToggleBlocking();
    }

    if (requests.applyProcessSelection) {
        ApplyProcessSelection();
    }

    if (requests.toggleClickThrough) {
        overlay_->SetClickThrough(state.clickThrough);
        config_->config().clickThrough = state.clickThrough;
        SetStatusOk(state.clickThrough ? L"Click-through enabled" : L"Click-through disabled");
    }

    if (requests.toggleOverlay) {
        overlay_->SetVisible(state.overlayVisible);
        config_->config().overlayVisible = state.overlayVisible;
    }

    if (requests.toggleTopmost) {
        overlay_->SetTopmost(state.topmost);
        config_->config().topmost = state.topmost;
    }

    if (requests.toggleExcludeFromCapture) {
        if (!overlay_->SetExcludeFromCapture(state.excludeFromCapture)) {
            state.excludeFromCapture = false;
            SetStatusError(L"SetWindowDisplayAffinity is not supported for this window");
        } else {
            config_->config().excludeFromCapture = state.excludeFromCapture;
        }
    }

    if (requests.toggleNoActivate) {
        overlay_->SetNoActivate(state.noActivate);
        config_->config().noActivate = state.noActivate;
        SetStatusOk(state.noActivate ? L"Game mode: the window will not take focus"
                                     : L"Normal mode: the window may take focus");
    }

    if (requests.toggleShowInTaskbar) {
        overlay_->SetShowInTaskbar(state.showInTaskbar);
        config_->config().showInTaskbar = state.showInTaskbar;
    }

    if (requests.setLanguage) {
        config_->config().language = requests.newLanguage;
        state.language = requests.newLanguage;
    }

    // --- window movement / resizing (declared by the UI each frame) ---------
    if (requests.windowDrag) {
        overlay_->MoveBy(static_cast<int>(std::lround(requests.windowDragX)),
                         static_cast<int>(std::lround(requests.windowDragY)));
        StoreWindowGeometry();
    }
    if (requests.windowResize) {
        overlay_->ResizeBy(static_cast<int>(std::lround(requests.windowResizeX)),
                           static_cast<int>(std::lround(requests.windowResizeY)));
        StoreWindowGeometry();
    }
    if (requests.resetWindowPosition) {
        overlay_->ResetPosition();
        EnsureWindowOnScreen();
    }

    if (requests.terminateConnectionsNow) {
        BlockTarget target;
        std::wstring resolveError;
        if (ResolveBlockTarget(target, resolveError)) {
            TerminateTargetConnections(target);
        } else {
            SetStatusError(resolveError);
        }
    }

    if (requests.processOptionsChanged) {
        processMonitor_->options().includeSystemProcesses = state.showSystemProcesses;
        config_->config().showSystemProcesses = state.showSystemProcesses;
        RefreshProcesses(true);
    } else if (requests.refreshProcesses) {
        RefreshProcesses(true);
    }

    if (requests.reloadGif) {
        loadedGifPath_.clear();
        ApplyGifIfNeeded(true);
    }

    if (requests.rebindHotkey) {
        std::wstring rebindError;
        if (RebindHotkey(requests.rebindAction, requests.reboundBinding, rebindError)) {
            hotkeyError_.clear();
            switch (requests.rebindAction) {
                case HotkeyAction::ToggleBlocking:     state.toggleBlocking = requests.reboundBinding; break;
                case HotkeyAction::ToggleClickThrough: state.toggleClickThrough = requests.reboundBinding; break;
                case HotkeyAction::ToggleOverlay:      state.toggleOverlay = requests.reboundBinding; break;
                case HotkeyAction::ToggleBorder:       state.toggleBorder = requests.reboundBinding; break;
            }
            SetStatusOk(std::wstring(L"Hotkey updated: ") +
                        FormatHotkeyText(requests.reboundBinding.modifiers,
                                         requests.reboundBinding.virtualKey));
        } else {
            hotkeyError_ = rebindError;
            SetStatusError(rebindError);
        }
    }

    if (requests.saveConfig) {
        SaveUiStateToConfig(state);
        std::wstring saveError;
        if (config_->Save(saveError)) {
            SetStatusOk(L"Settings saved to " + config_->path());
        } else {
            SetStatusError(saveError);
        }
    }

    if (requests.reloadConfig) {
        std::wstring loadError;
        if (config_->Load(loadError)) {
            logger_->SetMinimumLevel(LevelFromInt(config_->config().logLevel));
            refreshIntervalSeconds_ = static_cast<double>(config_->config().refreshIntervalMs) / 1000.0;
            LoadConfigIntoUiState();
            processMonitor_->options().includeSystemProcesses = config_->config().showSystemProcesses;
            RefreshProcesses(true);
            RegisterHotkeysFromConfig();
            SetStatusOk(loadError.empty() ? L"Settings reloaded" : loadError);
        } else {
            SetStatusError(loadError);
        }
    }
}

void Application::SetStatus(std::wstring message, bool isError)
{
    statusLine_ = std::move(message);
    statusIsError_ = isError;
    if (logger_ != nullptr) {
        if (isError) {
            LogError(*logger_, "APP", Utf8FromWide(statusLine_));
        } else {
            LogInfo(*logger_, "APP", Utf8FromWide(statusLine_));
        }
    }
}

} // namespace universalfnaf
