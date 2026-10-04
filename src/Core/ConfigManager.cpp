#include "universalfnaf/Core/ConfigManager.h"

#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace universalfnaf {
namespace {

using Json = nlohmann::json;

constexpr int kConfigVersion = 1;

// --- non-throwing readers ---------------------------------------------------
bool GetBool(const Json& node, const char* key, bool fallback)
{
    if (!node.is_object()) {
        return fallback;
    }
    const auto iterator = node.find(key);
    if (iterator == node.end() || !iterator->is_boolean()) {
        return fallback;
    }
    return iterator->get<bool>();
}

long long GetInt(const Json& node, const char* key, long long fallback)
{
    if (!node.is_object()) {
        return fallback;
    }
    const auto iterator = node.find(key);
    if (iterator == node.end() || !iterator->is_number()) {
        return fallback;
    }
    return static_cast<long long>(iterator->get<double>());
}

double GetFloat(const Json& node, const char* key, double fallback)
{
    if (!node.is_object()) {
        return fallback;
    }
    const auto iterator = node.find(key);
    if (iterator == node.end() || !iterator->is_number()) {
        return fallback;
    }
    return iterator->get<double>();
}

std::wstring GetWide(const Json& node, const char* key, const std::wstring& fallback)
{
    if (!node.is_object()) {
        return fallback;
    }
    const auto iterator = node.find(key);
    if (iterator == node.end() || !iterator->is_string()) {
        return fallback;
    }
    return WideFromUtf8(iterator->get<std::string>());
}

Json SubNode(const Json& root, const char* key)
{
    if (!root.is_object()) {
        return Json::object();
    }
    const auto iterator = root.find(key);
    if (iterator == root.end() || !iterator->is_object()) {
        return Json::object();
    }
    return *iterator;
}

HotkeyBinding GetHotkey(const Json& root, const char* key, const HotkeyBinding& fallback)
{
    const Json node = SubNode(root, key);
    if (node.empty()) {
        return fallback;
    }
    HotkeyBinding binding;
    binding.modifiers = static_cast<unsigned>(
        GetInt(node, "modifiers", static_cast<long long>(fallback.modifiers)));
    binding.virtualKey = static_cast<unsigned>(
        GetInt(node, "virtualKey", static_cast<long long>(fallback.virtualKey)));
    if (binding.virtualKey == 0) {
        return fallback;
    }
    return binding;
}

Json WriteHotkey(const HotkeyBinding& binding)
{
    return Json{{"modifiers", binding.modifiers}, {"virtualKey", binding.virtualKey}};
}

std::string ToUtf8(const std::wstring& text)
{
    return Utf8FromWide(text);
}

std::wstring ModeToString(TransparencyMode mode)
{
    return mode == TransparencyMode::ColorKeyLayered ? L"color-key" : L"direct-composition";
}

TransparencyMode ModeFromString(const std::wstring& text, TransparencyMode fallback)
{
    if (text == L"color-key" || text == L"layered") {
        return TransparencyMode::ColorKeyLayered;
    }
    if (text == L"direct-composition" || text == L"dcomp") {
        return TransparencyMode::DirectComposition;
    }
    return fallback;
}

bool DirectoryIsWritable(const std::wstring& directory)
{
    if (directory.empty()) {
        return false;
    }
    const std::wstring probe = JoinPath(directory, L".universalfnaf-write-probe.tmp");
    HANDLE raw = ::CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
        return false;
    }
    ::CloseHandle(raw);
    return true;
}

} // namespace

ConfigManager::ConfigManager(ILogger& logger, std::wstring path)
    : logger_(logger), path_(std::move(path))
{
}

std::wstring ConfigManager::DefaultPath()
{
    const std::wstring executableDirectory = GetExecutableDirectory();
    if (DirectoryIsWritable(executableDirectory)) {
        return JoinPath(executableDirectory, L"config.json");
    }
    const std::wstring localAppData = GetLocalAppDataDirectory();
    if (!localAppData.empty()) {
        const std::wstring directory = JoinPath(localAppData, L"UniversalFnaf");
        ::CreateDirectoryW(directory.c_str(), nullptr);
        return JoinPath(directory, L"config.json");
    }
    return JoinPath(executableDirectory, L"config.json");
}

void ConfigManager::ClampToReasonableRanges()
{
    config_.borderThickness = std::clamp(config_.borderThickness, 1.0f, 24.0f);
    config_.hueSpeed = std::clamp(config_.hueSpeed, 0.0f, 2.0f);
    config_.borderSaturation = std::clamp(config_.borderSaturation, 0.0f, 1.0f);
    config_.borderValue = std::clamp(config_.borderValue, 0.2f, 1.0f);
    config_.gifScale = std::clamp(config_.gifScale, 0.25f, 3.0f);
    config_.refreshIntervalMs = std::clamp(config_.refreshIntervalMs, 250, 10000);
    config_.windowWidth = std::clamp(config_.windowWidth, 460, 4096);
    config_.windowHeight = std::clamp(config_.windowHeight, 360, 4096);
    config_.windowX = std::clamp(config_.windowX, -32000, 32000);
    config_.windowY = std::clamp(config_.windowY, -32000, 32000);
    config_.logLevel = std::clamp(config_.logLevel, 0, 4);
}

bool ConfigManager::Load(std::wstring& error)
{
    error.clear();
    usedDefaults_ = true;
    config_ = AppConfig{};

    std::ifstream file(path_, std::ios::binary);
    if (!file.is_open()) {
        LogInfo(logger_, "CONFIG", Sprintf("no config at '%s', using defaults",
                                           Utf8FromWide(path_).c_str()));
        ClampToReasonableRanges();
        return true;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    // Tolerate a UTF-8 BOM written by Notepad or another editor.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }

    Json root;
    try {
        root = Json::parse(text, nullptr, true, true /* ignore_comments */);
    } catch (const std::exception& exception) {
        error = L"config.json is not valid JSON: " + WideFromUtf8(exception.what());
        LogError(logger_, "CONFIG", Utf8FromWide(error));
        ClampToReasonableRanges();
        return false;
    }

    if (!root.is_object()) {
        error = L"config.json must contain a JSON object";
        LogError(logger_, "CONFIG", Utf8FromWide(error));
        ClampToReasonableRanges();
        return false;
    }

    const Json target = SubNode(root, "target");
    config_.targetProcessId =
        static_cast<std::uint32_t>(GetInt(target, "processId", 0));
    config_.targetImageName = GetWide(target, "imageName", L"");
    config_.targetImagePath = GetWide(target, "imagePath", L"");

    const Json hotkeys = SubNode(root, "hotkeys");
    config_.toggleBlocking = GetHotkey(hotkeys, "toggleBlocking", config_.toggleBlocking);
    config_.toggleClickThrough = GetHotkey(hotkeys, "toggleClickThrough", config_.toggleClickThrough);
    config_.toggleOverlay = GetHotkey(hotkeys, "toggleOverlay", config_.toggleOverlay);
    config_.toggleBorder = GetHotkey(hotkeys, "toggleBorder", config_.toggleBorder);

    const Json network = SubNode(root, "network");
    config_.blockOnStartup = GetBool(network, "blockOnStartup", config_.blockOnStartup);
    config_.removeRulesOnExit = GetBool(network, "removeRulesOnExit", config_.removeRulesOnExit);
    config_.terminateExistingConnections = GetBool(network, "terminateExistingConnections",
                                                   config_.terminateExistingConnections);

    const Json window = SubNode(root, "window");
    config_.windowX = static_cast<int>(GetInt(window, "x", config_.windowX));
    config_.windowY = static_cast<int>(GetInt(window, "y", config_.windowY));
    config_.windowWidth = static_cast<int>(GetInt(window, "width", config_.windowWidth));
    config_.windowHeight = static_cast<int>(GetInt(window, "height", config_.windowHeight));
    config_.showInTaskbar = GetBool(window, "showInTaskbar", config_.showInTaskbar);

    const Json ui = SubNode(root, "ui");
    config_.language = LanguageFromTag(GetWide(ui, "language", LanguageTag(config_.language)));

    const Json overlay = SubNode(root, "overlay");
    config_.transparencyMode = ModeFromString(
        GetWide(overlay, "transparencyMode", ModeToString(config_.transparencyMode)),
        config_.transparencyMode);
    config_.clickThrough = GetBool(overlay, "clickThrough", config_.clickThrough);
    config_.overlayVisible = GetBool(overlay, "visible", config_.overlayVisible);
    config_.topmost = GetBool(overlay, "topmost", config_.topmost);
    config_.noActivate = GetBool(overlay, "noActivate", config_.noActivate);
    config_.excludeFromCapture = GetBool(overlay, "excludeFromCapture", config_.excludeFromCapture);

    const Json border = SubNode(overlay, "border");
    config_.borderEnabled = GetBool(border, "enabled", config_.borderEnabled);
    config_.borderThickness = static_cast<float>(GetFloat(border, "thickness", config_.borderThickness));
    config_.hueSpeed = static_cast<float>(GetFloat(border, "hueSpeed", config_.hueSpeed));
    config_.borderSaturation = static_cast<float>(GetFloat(border, "saturation", config_.borderSaturation));
    config_.borderValue = static_cast<float>(GetFloat(border, "value", config_.borderValue));
    config_.borderGlow = GetBool(border, "glow", config_.borderGlow);

    const Json gif = SubNode(overlay, "gif");
    config_.gifEnabled = GetBool(gif, "enabled", config_.gifEnabled);
    config_.gifScale = static_cast<float>(GetFloat(gif, "scale", config_.gifScale));
    config_.gifPath = GetWide(gif, "path", config_.gifPath);

    const Json processes = SubNode(root, "processes");
    config_.showSystemProcesses = GetBool(processes, "showSystemProcesses", config_.showSystemProcesses);
    config_.refreshIntervalMs =
        static_cast<int>(GetInt(processes, "refreshIntervalMs", config_.refreshIntervalMs));

    const Json logging = SubNode(root, "logging");
    config_.logLevel = static_cast<int>(GetInt(logging, "level", config_.logLevel));

    ClampToReasonableRanges();
    usedDefaults_ = false;

    LogInfo(logger_, "CONFIG", Sprintf("loaded '%s' (version %lld)", Utf8FromWide(path_).c_str(),
                                       GetInt(root, "version", kConfigVersion)));
    return true;
}

bool ConfigManager::Save(std::wstring& error)
{
    error.clear();
    ClampToReasonableRanges();

    Json root;
    root["version"] = kConfigVersion;
    root["target"] = Json{
        {"processId", config_.targetProcessId},
        {"imageName", ToUtf8(config_.targetImageName)},
        {"imagePath", ToUtf8(config_.targetImagePath)},
    };
    root["hotkeys"] = Json{
        {"toggleBlocking", WriteHotkey(config_.toggleBlocking)},
        {"toggleClickThrough", WriteHotkey(config_.toggleClickThrough)},
        {"toggleOverlay", WriteHotkey(config_.toggleOverlay)},
        {"toggleBorder", WriteHotkey(config_.toggleBorder)},
    };
    root["network"] = Json{
        {"blockOnStartup", config_.blockOnStartup},
        {"removeRulesOnExit", config_.removeRulesOnExit},
        {"terminateExistingConnections", config_.terminateExistingConnections},
    };
    root["window"] = Json{
        {"x", config_.windowX},
        {"y", config_.windowY},
        {"width", config_.windowWidth},
        {"height", config_.windowHeight},
        {"showInTaskbar", config_.showInTaskbar},
    };
    root["ui"] = Json{
        {"language", ToUtf8(LanguageTag(config_.language))},
    };
    root["overlay"] = Json{
        {"transparencyMode", ToUtf8(ModeToString(config_.transparencyMode))},
        {"clickThrough", config_.clickThrough},
        {"visible", config_.overlayVisible},
        {"topmost", config_.topmost},
        {"noActivate", config_.noActivate},
        {"excludeFromCapture", config_.excludeFromCapture},
        {"border", Json{
            {"enabled", config_.borderEnabled},
            {"thickness", config_.borderThickness},
            {"hueSpeed", config_.hueSpeed},
            {"saturation", config_.borderSaturation},
            {"value", config_.borderValue},
            {"glow", config_.borderGlow},
        }},
        {"gif", Json{
            {"enabled", config_.gifEnabled},
            {"scale", config_.gifScale},
            {"path", ToUtf8(config_.gifPath)},
        }},
    };
    root["processes"] = Json{
        {"showSystemProcesses", config_.showSystemProcesses},
        {"refreshIntervalMs", config_.refreshIntervalMs},
    };
    root["logging"] = Json{{"level", config_.logLevel}};

    const std::wstring temporaryPath = path_ + L".tmp";
    {
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            error = L"Cannot write " + temporaryPath;
            LogError(logger_, "CONFIG", Utf8FromWide(error));
            return false;
        }
        file << root.dump(2) << '\n';
        file.flush();
        if (!file.good()) {
            error = L"Write error while saving " + temporaryPath;
            LogError(logger_, "CONFIG", Utf8FromWide(error));
            return false;
        }
    }

    if (::MoveFileExW(temporaryPath.c_str(), path_.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        const DWORD lastError = ::GetLastError();
        ::DeleteFileW(temporaryPath.c_str());
        error = L"MoveFileExW failed: " + FormatWin32Message(lastError);
        LogError(logger_, "CONFIG", Utf8FromWide(error));
        return false;
    }

    LogInfo(logger_, "CONFIG", Sprintf("saved '%s'", Utf8FromWide(path_).c_str()));
    return true;
}

} // namespace universalfnaf
