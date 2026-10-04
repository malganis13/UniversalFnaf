#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Core/ConfigManager.h
//  config.json persistence (UTF-8, atomic replace, forward/backward tolerant).
//  Unknown keys are ignored and missing keys fall back to safe defaults, so a
//  hand-edited or partially written file can never prevent the utility from
//  starting.
// ---------------------------------------------------------------------------

#include "universalfnaf/Common/Localization.h"
#include "universalfnaf/Common/TransparencyMode.h"
#include "universalfnaf/Input/IHotkeyManager.h"

#include <cstdint>
#include <string>

namespace universalfnaf {

class ILogger;

struct AppConfig {
    // --- block target -------------------------------------------------------
    std::uint32_t targetProcessId = 0;
    std::wstring targetImageName;
    std::wstring targetImagePath;

    // --- hotkeys ------------------------------------------------------------
    HotkeyBinding toggleBlocking{MOD_CONTROL | MOD_ALT, VK_F8};
    HotkeyBinding toggleClickThrough{MOD_CONTROL | MOD_ALT, VK_F9};
    HotkeyBinding toggleOverlay{MOD_CONTROL | MOD_ALT, VK_F10};
    HotkeyBinding toggleBorder{MOD_CONTROL | MOD_ALT, VK_F11};

    // --- network ------------------------------------------------------------
    bool blockOnStartup = false;
    bool removeRulesOnExit = true;
    // Immediately delete already established outbound TCP sessions of the
    // target when a block is engaged (IPHLPAPI, admin only).
    bool terminateExistingConnections = true;

    // --- window geometry ----------------------------------------------------
    int windowX = 80;
    int windowY = 80;
    int windowWidth = 620;
    int windowHeight = 800;
    bool showInTaskbar = true;

    // --- user interface -----------------------------------------------------
    UiLanguage language = UiLanguage::Russian;

    // --- overlay ------------------------------------------------------------
    TransparencyMode transparencyMode = TransparencyMode::DirectComposition;
    bool clickThrough = false;
    bool overlayVisible = true;
    bool topmost = true;
    bool noActivate = false;   // game mode off by default: text fields must work
    bool excludeFromCapture = false;

    // --- RGB border ---------------------------------------------------------
    bool borderEnabled = true;
    float borderThickness = 3.0f;
    float hueSpeed = 0.35f;
    float borderSaturation = 1.0f;
    float borderValue = 1.0f;
    bool borderGlow = true;

    // --- GIF ----------------------------------------------------------------
    bool gifEnabled = true;
    float gifScale = 1.0f;
    std::wstring gifPath;

    // --- process list -------------------------------------------------------
    bool showSystemProcesses = false;
    int refreshIntervalMs = 1000;

    // --- logging ------------------------------------------------------------
    int logLevel = 2;   // LogLevel::Info
};

class ConfigManager {
public:
    ConfigManager(ILogger& logger, std::wstring path);

    // config.json next to the executable when that directory is writable,
    // otherwise %LOCALAPPDATA%\UniversalFnaf\config.json.
    static std::wstring DefaultPath();

    // A missing file is not an error: defaults are used and `error` stays empty.
    bool Load(std::wstring& error);
    // Written to <path>.tmp and then atomically moved over the old file.
    bool Save(std::wstring& error);

    AppConfig& config() noexcept { return config_; }
    const AppConfig& config() const noexcept { return config_; }

    const std::wstring& path() const noexcept { return path_; }
    bool lastLoadUsedDefaults() const noexcept { return usedDefaults_; }

private:
    void ClampToReasonableRanges();

    ILogger& logger_;
    std::wstring path_;
    AppConfig config_{};
    bool usedDefaults_ = true;
};

} // namespace universalfnaf
