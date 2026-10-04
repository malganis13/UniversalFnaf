#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: UI/UIManager.h
//
//  Dear ImGui (DirectX 11) front-end. The ImGui panel *is* the whole window:
//    * RGB border drawn around this window's own perimeter (hue animates)
//    * animated GIF in the window's top-left corner
//    * header is the drag handle, bottom-right corner is the resize grip
//    * RU / ENG switch, process picker with icons, hotkey assignment
//
//  The UI never calls SetWindowsHookEx, never injects anything and never draws
//  into another process: it renders exclusively into its own swap-chain buffer.
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "universalfnaf/Common/Localization.h"
#include "universalfnaf/Input/HotkeyCapture.h"
#include "universalfnaf/Input/IHotkeyManager.h"
#include "universalfnaf/Process/IProcessMonitor.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct ID3D11ShaderResourceView;

namespace universalfnaf {

class ILogger;
class Renderer;
class GifAnimator;

// Snapshot of everything the UI displays. The UI may edit the marked fields;
// the application reads them back after RenderFrame() and applies the changes.
struct UiState {
    // --- engine / status (read-only) ---------------------------------------
    bool engineReady = false;
    std::wstring engineStatus;
    bool blocking = false;
    std::wstring targetName;
    std::wstring targetPath;
    std::uint32_t targetPid = 0;
    std::size_t activeRuleCount = 0;

    std::wstring statusLine;
    bool statusIsError = false;
    std::wstring configPath;
    std::wstring logPath;
    std::wstring versionLine;

    double framesPerSecond = 0.0;
    double uptimeSeconds = 0.0;

    // --- localisation -------------------------------------------------------
    UiLanguage language = UiLanguage::Russian;

    // --- process picker -----------------------------------------------------
    std::vector<ProcessInfo> processes;
    int selectedProcessIndex = -1;
    std::wstring processListError;
    double processListAgeSeconds = 0.0;

    // --- window behaviour (editable) ---------------------------------------
    bool clickThrough = false;
    bool overlayVisible = true;
    bool topmost = true;
    bool noActivate = false;         // "game mode"
    bool showInTaskbar = true;
    bool excludeFromCapture = false;

    // --- RGB border (editable) ---------------------------------------------
    bool borderEnabled = true;
    bool borderGlow = true;
    float borderThickness = 3.0f;
    float hueSpeed = 0.35f;
    float borderSaturation = 1.0f;
    float borderValue = 1.0f;

    // --- GIF (editable) -----------------------------------------------------
    bool gifEnabled = true;
    float gifScale = 1.0f;
    std::wstring gifPath;
    bool gifLoaded = false;
    int gifWidth = 0;
    int gifHeight = 0;
    std::size_t gifFrameCount = 0;
    std::wstring gifError;

    // --- network options (editable) ----------------------------------------
    bool blockOnStartup = false;
    bool terminateExistingConnections = true;

    // --- hotkeys ------------------------------------------------------------
    HotkeyBinding toggleBlocking{};
    HotkeyBinding toggleClickThrough{};
    HotkeyBinding toggleOverlay{};
    HotkeyBinding toggleBorder{};
    std::wstring hotkeyError;

    // --- process options ----------------------------------------------------
    bool showSystemProcesses = false;
};

// Everything the UI wants the application to do after this frame.
struct UiRequests {
    bool toggleBlocking = false;
    bool toggleClickThrough = false;
    bool toggleOverlay = false;
    bool toggleTopmost = false;
    bool toggleBorder = false;
    bool toggleExcludeFromCapture = false;
    bool toggleNoActivate = false;
    bool toggleShowInTaskbar = false;

    bool terminateConnectionsNow = false;
    bool resetWindowPosition = false;

    bool saveConfig = false;
    bool reloadConfig = false;
    bool refreshProcesses = false;
    bool reloadGif = false;
    bool applyProcessSelection = false;
    bool processOptionsChanged = false;

    bool setLanguage = false;
    UiLanguage newLanguage = UiLanguage::Russian;

    bool rebindHotkey = false;
    HotkeyAction rebindAction = HotkeyAction::ToggleBlocking;
    HotkeyBinding reboundBinding{};

    // Window drag / resize. Only "begin" and "active" are published: the
    // application reads the absolute cursor position with Win32 GetCursorPos(),
    // because ImGui mouse coordinates are window relative - moving the window
    // would feed back into the per-frame delta and make the drag oscillate.
    bool windowDragBegin = false;
    bool windowDragActive = false;
    bool windowResizeBegin = false;
    bool windowResizeActive = false;

    bool quit = false;
};

class UIManager {
public:
    UIManager(ILogger& logger, Renderer& renderer, const GifAnimator& gifAnimator);
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool Initialize(HWND window, std::wstring& error);
    void Shutdown();

    // Starts an ImGui frame (Win32 + DX11 backends).
    void NewFrame();

    // Draws the window frame, decorations and the panel, then submits the draw
    // data to the DirectX 11 backend. Call between Renderer::BeginFrame/EndFrame.
    void RenderFrame(UiState& state, UiRequests& requests, double deltaSeconds);

    // Called from the window procedure.
    std::optional<LRESULT> ProcessWindowMessage(HWND window, UINT message,
                                                WPARAM wParam, LPARAM lParam);

    // The DirectX 11 backend owns device objects; recreate them after a resize
    // or a device reset.
    void InvalidateDeviceObjects();
    void RecreateDeviceObjects();

    // Rebuilds the style for a new monitor DPI (the font is loaded once, so the
    // glyphs are rescaled through FontGlobalScale).
    void SetDpiScale(float scale);

    void SetStatus(UiState& state, std::wstring message, bool isError);

private:
    const UiStrings& Strings() const noexcept { return *strings_; }

    // Frame decoration: the animated RGB ring around this window's perimeter.
    void DrawWindowBorder(const UiState& state);

    // Sections.
    void DrawHeaderBar(UiState& state, UiRequests& requests);
    void DrawResizeGrip(UiRequests& requests);
    void DrawTrafficSection(UiState& state, UiRequests& requests);
    void DrawProcessSection(UiState& state, UiRequests& requests);
    void DrawNetworkSection(UiState& state, UiRequests& requests);
    void DrawHotkeySection(UiState& state, UiRequests& requests);
    void DrawWindowSection(UiState& state, UiRequests& requests);
    void DrawBorderSection(UiState& state, UiRequests& requests);
    void DrawGifSection(UiState& state, UiRequests& requests);
    void DrawFooter(UiState& state, UiRequests& requests);

    void DrawHotkeyEditor(const char* label, const char* tooltip,
                          HotkeyAction action, const HotkeyBinding& binding,
                          UiRequests& requests);
    void UpdateHotkeyCapture(UiState& state, UiRequests& requests);

    bool BrowseForGifFile(std::wstring& outPath);

    ID3D11ShaderResourceView* IconTextureFor(const ProcessInfo& info);
    ID3D11ShaderResourceView* GifTextureFor(std::size_t frameIndex);
    void ReleaseTextureCache();

    static std::string Narrow(const std::wstring& text);

    ILogger& logger_;
    Renderer& renderer_;
    const GifAnimator& gifAnimator_;

    HWND window_ = nullptr;
    bool imguiInitialized_ = false;
    bool deviceObjectsValid_ = true;
    float dpiScale_ = 1.0f;
    float fontDpiScale_ = 1.0f;   // DPI the font atlas was rasterised for

    float huePhase_ = 0.0f;
    double elapsedSeconds_ = 0.0;
    const UiStrings* strings_ = &kUiStringsRussian;

    HotkeyCapture capture_;
    std::wstring captureMessage_;

    char processFilter_[64]{};
    std::wstring gifPathBufferOwner_;
    char gifPathBuffer_[1024]{};

    std::unordered_map<std::wstring, ID3D11ShaderResourceView*> iconTextures_;
    std::unordered_map<std::size_t, ID3D11ShaderResourceView*> gifTextures_;
    std::wstring gifTextureOwner_;
};

} // namespace universalfnaf
