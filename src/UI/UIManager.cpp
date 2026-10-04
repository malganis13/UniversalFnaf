#include "universalfnaf/UI/UIManager.h"

#include "universalfnaf/Common/GdiPlusSession.h"
#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"
#include "universalfnaf/UI/GifAnimator.h"
#include "universalfnaf/UI/Renderer.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

// imgui_impl_win32.h deliberately does not declare the message handler (it
// would have to pull in <windows.h>); the official example forward-declares it,
// and so do we.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message,
                                                             WPARAM wParam, LPARAM lParam);

namespace universalfnaf {
namespace {

constexpr float kPi = 3.14159265358979323846f;

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

ImTextureID ToTextureId(ID3D11ShaderResourceView* view)
{
    return reinterpret_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(view));
}

ImU32 HueColor(float hue, float saturation, float value, float alpha = 1.0f)
{
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    HsvToRgb(hue, saturation, value, red, green, blue);
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(red, green, blue, std::clamp(alpha, 0.0f, 1.0f)));
}

void ApplyDarkTheme(float dpiScale)
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ScrollbarSize = 12.0f;
    style.ScaleAllSizes(dpiScale);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.062f, 0.082f, 0.97f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.19f, 1.00f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.21f, 0.27f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.17f, 0.20f, 0.27f, 1.00f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.30f, 0.41f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.35f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.34f, 0.46f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(1.00f, 1.00f, 1.00f, 0.14f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
}

// --- rounded-rectangle perimeter sampling -----------------------------------
// Produces a closed path around a rounded rectangle: centre-line points, their
// outward normals and the normalised arc position (0..1). Drawing per-segment
// quads with a hue derived from the arc position yields a seamless rainbow that
// hugs the window.
struct PerimeterPath {
    std::vector<ImVec2> centers;
    std::vector<ImVec2> normals;
    std::vector<float> arc;
};

PerimeterPath BuildRoundedRectPath(const ImVec2& min, const ImVec2& max, float rounding,
                                   int edgeSegments, int cornerSegments)
{
    PerimeterPath path;
    const float left = min.x;
    const float top = min.y;
    const float right = std::max(max.x, min.x + 1.0f);
    const float bottom = std::max(max.y, min.y + 1.0f);
    const float radius = std::max(
        0.0f, std::min(rounding, std::min((right - left) * 0.5f, (bottom - top) * 0.5f)));

    const auto push = [&path](const ImVec2& point, const ImVec2& normal) {
        path.centers.push_back(point);
        path.normals.push_back(normal);
    };
    const auto edge = [&](const ImVec2& from, const ImVec2& to, const ImVec2& normal) {
        for (int i = 0; i < edgeSegments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(edgeSegments);
            push(ImVec2(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t), normal);
        }
    };
    const auto corner = [&](const ImVec2& center, float startAngle, float endAngle) {
        for (int i = 0; i <= cornerSegments; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(cornerSegments);
            const float angle = startAngle + (endAngle - startAngle) * t;
            const ImVec2 normal(std::cos(angle), std::sin(angle));
            push(ImVec2(center.x + normal.x * radius, center.y + normal.y * radius), normal);
        }
    };

    const ImVec2 topLeftCorner(left + radius, top + radius);
    const ImVec2 topRightCorner(right - radius, top + radius);
    const ImVec2 bottomRightCorner(right - radius, bottom - radius);
    const ImVec2 bottomLeftCorner(left + radius, bottom - radius);

    edge(ImVec2(left + radius, top), ImVec2(right - radius, top), ImVec2(0.0f, -1.0f));
    corner(topRightCorner, -kPi * 0.5f, 0.0f);
    edge(ImVec2(right, top + radius), ImVec2(right, bottom - radius), ImVec2(1.0f, 0.0f));
    corner(bottomRightCorner, 0.0f, kPi * 0.5f);
    edge(ImVec2(right - radius, bottom), ImVec2(left + radius, bottom), ImVec2(0.0f, 1.0f));
    corner(bottomLeftCorner, kPi * 0.5f, kPi);
    edge(ImVec2(left, bottom - radius), ImVec2(left, top + radius), ImVec2(-1.0f, 0.0f));
    corner(topLeftCorner, kPi, kPi * 1.5f);

    if (path.centers.size() > 1) {
        const ImVec2& first = path.centers.front();
        const ImVec2& last = path.centers.back();
        if (std::fabs(first.x - last.x) < 0.01f && std::fabs(first.y - last.y) < 0.01f) {
            path.centers.pop_back();
            path.normals.pop_back();
        }
    }

    // Normalised arc length so the hue is distributed evenly along the border.
    const std::size_t count = path.centers.size();
    path.arc.assign(count, 0.0f);
    if (count < 2) {
        return path;
    }
    std::vector<float> cumulative(count, 0.0f);
    for (std::size_t i = 1; i < count; ++i) {
        const ImVec2 delta(path.centers[i].x - path.centers[i - 1].x,
                           path.centers[i].y - path.centers[i - 1].y);
        cumulative[i] = cumulative[i - 1] + std::sqrt(delta.x * delta.x + delta.y * delta.y);
    }
    const ImVec2 closing(path.centers[0].x - path.centers[count - 1].x,
                         path.centers[0].y - path.centers[count - 1].y);
    const float total = cumulative[count - 1] + std::sqrt(closing.x * closing.x + closing.y * closing.y);
    if (total <= 0.0f) {
        return path;
    }
    for (std::size_t i = 0; i < count; ++i) {
        path.arc[i] = cumulative[i] / total;
    }
    return path;
}

// Emits one closed band of quads: from the path centre line offset by
// `outerOffset` (positive = outward) to `innerOffset`.
void EmitHueRing(ImDrawList* drawList, const PerimeterPath& path,
                 float outerOffset, float innerOffset, float alpha,
                 float hueBase, float saturation, float value)
{
    const std::size_t count = path.centers.size();
    if (count < 4 || alpha <= 0.0f) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t j = (i + 1) % count;
        const ImVec2& centerI = path.centers[i];
        const ImVec2& normalI = path.normals[i];
        const ImVec2& centerJ = path.centers[j];
        const ImVec2& normalJ = path.normals[j];

        const ImVec2 outerI(centerI.x + normalI.x * outerOffset, centerI.y + normalI.y * outerOffset);
        const ImVec2 innerI(centerI.x + normalI.x * innerOffset, centerI.y + normalI.y * innerOffset);
        const ImVec2 outerJ(centerJ.x + normalJ.x * outerOffset, centerJ.y + normalJ.y * outerOffset);
        const ImVec2 innerJ(centerJ.x + normalJ.x * innerOffset, centerJ.y + normalJ.y * innerOffset);

        // The closing segment wraps to arc 1.0, which equals arc 0.0 with a
        // full-spectrum spread - the loop stays seamless.
        const float arcJ = (j == 0) ? 1.0f : path.arc[j];
        const float position = (path.arc[i] + arcJ) * 0.5f;
        drawList->AddQuadFilled(outerI, outerJ, innerJ, innerI,
                                HueColor(hueBase + position, saturation, value, alpha));
    }
}

} // namespace

UIManager::UIManager(ILogger& logger, Renderer& renderer, const GifAnimator& gifAnimator)
    : logger_(logger), renderer_(renderer), gifAnimator_(gifAnimator)
{
}

UIManager::~UIManager()
{
    Shutdown();
}

std::string UIManager::Narrow(const std::wstring& text)
{
    return Utf8FromWide(text);
}

bool UIManager::Initialize(HWND window, std::wstring& error)
{
    error.clear();
    if (imguiInitialized_) {
        return true;
    }
    if (window == nullptr || !renderer_.initialized()) {
        error = L"UIManager::Initialize requires a window and an initialized renderer";
        return false;
    }

    window_ = window;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;          // our configuration lives in config.json
    io.LogFilename = nullptr;
    // Deliberately NOT ImGuiConfigFlags_NoMouseCursorChange: the resize grip and
    // text fields must be able to change the cursor.

    dpiScale_ = std::max(1.0f, static_cast<float>(::GetDpiForWindow(window)) / 96.0f);
    fontDpiScale_ = dpiScale_;

    // Segoe UI carries Latin + Cyrillic, which the default ImGui font lacks.
    const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, 16.0f * dpiScale_, nullptr,
                                                io.Fonts->GetGlyphRangesCyrillic());
    if (font == nullptr) {
        LogWarn(logger_, "UI", "segoeui.ttf not available, falling back to the built-in font");
        io.Fonts->AddFontDefault();
    }

    ApplyDarkTheme(dpiScale_);

    if (!ImGui_ImplWin32_Init(window)) {
        ImGui::DestroyContext();
        error = L"ImGui_ImplWin32_Init failed";
        return false;
    }
    if (!ImGui_ImplDX11_Init(renderer_.device(), renderer_.context())) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        error = L"ImGui_ImplDX11_Init failed";
        return false;
    }

    imguiInitialized_ = true;
    LogInfo(logger_, "UI", Sprintf("ImGui initialized (dpi scale %.2f)", dpiScale_));
    return true;
}

void UIManager::Shutdown()
{
    ReleaseTextureCache();

    if (imguiInitialized_) {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    window_ = nullptr;
}

void UIManager::InvalidateDeviceObjects()
{
    if (!imguiInitialized_) {
        return;
    }
    ReleaseTextureCache();
    ImGui_ImplDX11_InvalidateDeviceObjects();
    deviceObjectsValid_ = false;
}

void UIManager::RecreateDeviceObjects()
{
    if (!imguiInitialized_) {
        return;
    }
    ImGui_ImplDX11_CreateDeviceObjects();
    deviceObjectsValid_ = true;
}

void UIManager::SetDpiScale(float scale)
{
    if (!imguiInitialized_ || scale <= 0.0f) {
        return;
    }
    const float clamped = std::clamp(scale, 0.75f, 3.0f);
    if (std::fabs(clamped - dpiScale_) < 0.01f) {
        return;
    }
    dpiScale_ = clamped;
    ApplyDarkTheme(dpiScale_);
    // The atlas was rasterised at fontDpiScale_; scale the glyphs to match.
    ImGui::GetIO().FontGlobalScale = dpiScale_ / fontDpiScale_;
    LogInfo(logger_, "UI", Sprintf("dpi scale changed to %.2f", dpiScale_));
}

void UIManager::SetStatus(UiState& state, std::wstring message, bool isError)
{
    state.statusLine = std::move(message);
    state.statusIsError = isError;
}

std::optional<LRESULT> UIManager::ProcessWindowMessage(HWND window, UINT message,
                                                       WPARAM wParam, LPARAM lParam)
{
    if (!imguiInitialized_) {
        return std::nullopt;
    }
    const LRESULT handled = ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    if (handled != 0) {
        return handled;
    }
    return std::nullopt;
}

void UIManager::NewFrame()
{
    if (!imguiInitialized_) {
        return;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

// ---------------------------------------------------------------------------
//  Window frame decoration
// ---------------------------------------------------------------------------

void UIManager::DrawWindowBorder(const UiState& state)
{
    if (!state.borderEnabled) {
        return;
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 24.0f || display.y < 24.0f) {
        return;
    }

    const float thickness = std::clamp(state.borderThickness * dpiScale_, 1.0f, 16.0f);
    const float rounding = 16.0f * dpiScale_;
    const float inset = thickness * 0.5f + 1.0f;

    const PerimeterPath path =
        BuildRoundedRectPath(ImVec2(inset, inset), ImVec2(display.x - inset, display.y - inset),
                             rounding, 26, 8);

    const float hue = huePhase_ - std::floor(huePhase_);
    const float saturation = std::clamp(state.borderSaturation, 0.0f, 1.0f);
    const float value = std::clamp(state.borderValue, 0.05f, 1.0f);

    if (state.borderGlow) {
        // Neon bleed: one soft band inward, one thin halo outward.
        EmitHueRing(drawList, path, -thickness * 0.5f, -thickness * 2.1f, 0.20f,
                    hue, saturation, value);
        EmitHueRing(drawList, path, thickness * 1.4f, thickness * 0.5f, 0.18f,
                    hue, saturation, value);
    }
    EmitHueRing(drawList, path, thickness * 0.5f, -thickness * 0.5f, 1.0f,
                hue, saturation, value);
}

// ---------------------------------------------------------------------------
//  Frame
// ---------------------------------------------------------------------------

void UIManager::RenderFrame(UiState& state, UiRequests& requests, double deltaSeconds)
{
    if (!imguiInitialized_) {
        return;
    }

    strings_ = &StringsFor(state.language);

    elapsedSeconds_ += deltaSeconds;
    huePhase_ += static_cast<float>(deltaSeconds) * std::max(0.0f, state.hueSpeed);
    if (huePhase_ > 100000.0f) {
        huePhase_ = std::fmod(huePhase_, 1.0f);
    }

    UpdateHotkeyCapture(state, requests);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(display, ImGuiCond_Always);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoScrollWithMouse |
                                   ImGuiWindowFlags_NoNavFocus |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f * dpiScale_, 8.0f * dpiScale_));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##universalfnaf_panel", nullptr, flags);

    DrawHeaderBar(state, requests);
    ImGui::Separator();

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() * 2.0f + 8.0f * dpiScale_;
    ImGui::BeginChild("##content", ImVec2(0.0f, -footerHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_None);
    DrawTrafficSection(state, requests);
    DrawProcessSection(state, requests);
    DrawNetworkSection(state, requests);
    DrawHotkeySection(state, requests);
    DrawWindowSection(state, requests);
    DrawBorderSection(state, requests);
    DrawGifSection(state, requests);
    ImGui::EndChild();

    DrawFooter(state, requests);
    DrawResizeGrip(requests);

    ImGui::End();
    ImGui::PopStyleVar(2);

    // Background draw list: the ring sits behind the panel and hugs the window.
    DrawWindowBorder(state);

    ImGui::Render();
    if (deviceObjectsValid_) {
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

// ---------------------------------------------------------------------------
//  Header: GIF, title, drag handle, language switch, close
// ---------------------------------------------------------------------------

void UIManager::DrawHeaderBar(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float available = ImGui::GetContentRegionAvail().x;

    // --- GIF in the window's top-left corner --------------------------------
    float gifWidth = 0.0f;
    float gifHeight = 0.0f;
    if (state.gifEnabled && gifAnimator_.loaded()) {
        if (ID3D11ShaderResourceView* texture = GifTextureFor(gifAnimator_.currentIndex())) {
            const float scale = std::clamp(state.gifScale, 0.25f, 3.0f) * dpiScale_;
            gifWidth = static_cast<float>(gifAnimator_.width()) * scale;
            gifHeight = static_cast<float>(gifAnimator_.height()) * scale;
            const float maxHeight = 132.0f * dpiScale_;
            if (gifHeight > maxHeight && gifHeight > 0.0f) {
                const float factor = maxHeight / gifHeight;
                gifWidth *= factor;
                gifHeight *= factor;
            }
            drawList->AddImage(ToTextureId(texture), origin,
                               ImVec2(origin.x + gifWidth, origin.y + gifHeight));
        }
    }

    const float headerHeight = std::max(32.0f * dpiScale_, gifHeight);
    const float gap = 4.0f * dpiScale_;
    const float closeWidth = 30.0f * dpiScale_;
    const float languageWidth = 34.0f * dpiScale_;
    const float buttonsWidth = languageWidth * 2.0f + gap * 2.0f + closeWidth;
    const float textLeft = origin.x + (gifWidth > 0.0f ? gifWidth + 10.0f * dpiScale_ : 0.0f);
    const float buttonHeight = ImGui::GetFrameHeight();
    const float buttonTop = origin.y + (headerHeight - buttonHeight) * 0.5f;

    // --- drag handle (created first so the buttons win the hover test) -------
    const float stripWidth = std::max(24.0f * dpiScale_,
                                      available - (textLeft - origin.x) - buttonsWidth - gap * 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(textLeft, origin.y));
    ImGui::InvisibleButton("##window_drag", ImVec2(stripWidth, headerHeight));
    const bool dragHovered = ImGui::IsItemHovered();
    const bool dragActive = ImGui::IsItemActive();
    if (dragActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        requests.windowDrag = true;
        requests.windowDragX = delta.x;
        requests.windowDragY = delta.y;
    }
    if (dragHovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        if (!dragActive) {
            ImGui::SetTooltip("%s", text.dragHint);
        }
    }

    // --- title + version (drawn directly, no layout impact) ------------------
    const ImVec2 titleSize = ImGui::CalcTextSize(text.appTitle);
    const float textY = origin.y + (headerHeight - titleSize.y) * 0.5f;
    const float titleX = textLeft + 6.0f * dpiScale_;
    drawList->AddText(ImVec2(titleX, textY), ImGui::GetColorU32(ImGuiCol_Text), text.appTitle);

    const std::string version = Narrow(state.versionLine);
    if (!version.empty()) {
        const float versionX = titleX + titleSize.x + 8.0f * dpiScale_;
        drawList->AddText(ImVec2(versionX, textY + 1.0f * dpiScale_),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), version.c_str());
    }

    // --- language switch + close --------------------------------------------
    float cursorX = origin.x + available - buttonsWidth;
    const auto languageButton = [&](const char* label, UiLanguage language) {
        const bool active = state.language == language;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        ImGui::SetCursorScreenPos(ImVec2(cursorX, buttonTop));
        if (ImGui::Button(label, ImVec2(languageWidth, buttonHeight)) && !active) {
            requests.setLanguage = true;
            requests.newLanguage = language;
        }
        if (active) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", text.languageTip);
        }
        cursorX += languageWidth + gap;
    };
    languageButton("RU", UiLanguage::Russian);
    languageButton("EN", UiLanguage::English);

    ImGui::SetCursorScreenPos(ImVec2(cursorX, buttonTop));
    if (ImGui::Button("X", ImVec2(closeWidth, buttonHeight))) {
        requests.quit = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.closeTip);
    }

    // --- separator under the header -----------------------------------------
    const float separatorY = origin.y + headerHeight + 2.0f * dpiScale_;
    drawList->AddLine(ImVec2(origin.x, separatorY), ImVec2(origin.x + available, separatorY),
                      ImGui::GetColorU32(ImGuiCol_Separator));

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + headerHeight + 6.0f * dpiScale_));
}

void UIManager::DrawResizeGrip(UiRequests& requests)
{
    const ImVec2 windowPosition = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const float grip = 18.0f * dpiScale_;
    const float margin = 4.0f * dpiScale_;

    ImGui::SetCursorScreenPos(ImVec2(windowPosition.x + windowSize.x - grip - margin,
                                     windowPosition.y + windowSize.y - grip - margin));
    ImGui::InvisibleButton("##window_resize", ImVec2(grip, grip));

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    if (hovered || active) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        if (!active) {
            ImGui::SetTooltip("%s", Strings().resizeHint);
        }
    }
    if (active) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        requests.windowResize = true;
        requests.windowResizeX = delta.x;
        requests.windowResizeY = delta.y;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(hovered || active ? ImGuiCol_Text
                                                             : ImGuiCol_TextDisabled);
    const ImVec2 topLeft = ImGui::GetItemRectMin();
    for (int line = 1; line <= 3; ++line) {
        const float offset = static_cast<float>(line) * (grip / 4.0f);
        drawList->AddLine(ImVec2(topLeft.x + offset, topLeft.y + grip),
                          ImVec2(topLeft.x + grip, topLeft.y + offset), color, 1.4f * dpiScale_);
    }
}

// ---------------------------------------------------------------------------
//  Sections
// ---------------------------------------------------------------------------

void UIManager::DrawTrafficSection(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::SeparatorText(text.sectionTraffic);

    // --- state lamp ---------------------------------------------------------
    const ImVec4 lampColor = state.blocking ? ImVec4(1.0f, 0.25f, 0.25f, 1.0f)
                                            : ImVec4(0.25f, 0.9f, 0.4f, 1.0f);
    const ImVec2 lampCenter(ImGui::GetCursorScreenPos().x + 9.0f * dpiScale_,
                            ImGui::GetCursorScreenPos().y + 9.0f * dpiScale_);
    ImGui::GetWindowDrawList()->AddCircleFilled(lampCenter, 9.0f * dpiScale_,
                                                ImGui::GetColorU32(lampColor));
    ImGui::GetWindowDrawList()->AddCircle(lampCenter, 9.0f * dpiScale_,
                                          ImGui::GetColorU32(ImVec4(1, 1, 1, 0.35f)),
                                          0, 1.5f * dpiScale_);
    ImGui::Dummy(ImVec2(24.0f * dpiScale_, 18.0f * dpiScale_));
    ImGui::SameLine();
    ImGui::TextColored(lampColor, "%s",
                       state.blocking ? text.stateBlocked : text.stateAllowed);

    if (state.targetName.empty()) {
        ImGui::TextDisabled("%s", text.targetNone);
    } else {
        ImGui::Text(text.targetFormat, Narrow(state.targetName).c_str(),
                    static_cast<unsigned long>(state.targetPid));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(text.targetRulesTip, Narrow(state.targetPath).c_str(),
                              state.activeRuleCount);
        }
    }

    // --- big toggle ---------------------------------------------------------
    const ImVec4 buttonColor = state.blocking ? ImVec4(0.45f, 0.12f, 0.12f, 1.0f)
                                              : ImVec4(0.12f, 0.38f, 0.18f, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          state.blocking ? ImVec4(0.60f, 0.16f, 0.16f, 1.0f)
                                         : ImVec4(0.16f, 0.50f, 0.24f, 1.0f));
    if (ImGui::Button(state.blocking ? text.buttonAllow : text.buttonBlock,
                      ImVec2(-FLT_MIN, 34.0f * dpiScale_))) {
        requests.toggleBlocking = true;
    }
    ImGui::PopStyleColor(2);

    ImGui::TextDisabled(text.hotkeyFormat,
                        Narrow(FormatHotkeyText(state.toggleBlocking.modifiers,
                                                state.toggleBlocking.virtualKey)).c_str());

    ImGui::BeginDisabled(state.targetName.empty() && !state.blocking);
    if (ImGui::Button(text.terminateNow, ImVec2(-FLT_MIN, 0.0f))) {
        requests.terminateConnectionsNow = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text.terminateTip);
    }

    if (!state.statusLine.empty()) {
        const ImVec4 color = state.statusIsError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                                                 : ImVec4(0.65f, 0.85f, 1.0f, 1.0f);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(color, "%s", Narrow(state.statusLine).c_str());
        ImGui::PopTextWrapPos();
    }
}

void UIManager::DrawProcessSection(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::SeparatorText(text.sectionProcess);

    if (ImGui::Button(text.refreshList)) {
        requests.refreshProcesses = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(text.showSystem, &state.showSystemProcesses)) {
        requests.processOptionsChanged = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu)", state.processes.size());

    ImGui::BeginDisabled(state.noActivate);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", text.filterHint, processFilter_, sizeof(processFilter_));
    ImGui::EndDisabled();
    if (state.noActivate && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text.gameModeTip);
    }

    const std::wstring filter = ToLowerInvariant(WideFromUtf8(processFilter_));

    std::string preview = text.selectPlaceholder;
    if (state.selectedProcessIndex >= 0 &&
        static_cast<std::size_t>(state.selectedProcessIndex) < state.processes.size()) {
        const ProcessInfo& selected =
            state.processes[static_cast<std::size_t>(state.selectedProcessIndex)];
        preview = Narrow(selected.imageName + L"  (PID " + std::to_wstring(selected.processId) + L")");
    }

    if (ImGui::BeginCombo("##process_combo", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        int visibleCount = 0;
        for (std::size_t index = 0; index < state.processes.size(); ++index) {
            const ProcessInfo& info = state.processes[index];
            if (!filter.empty()) {
                const std::wstring haystack = ToLowerInvariant(
                    info.imageName + L" " + std::to_wstring(info.processId));
                if (haystack.find(filter) == std::wstring::npos) {
                    continue;
                }
            }
            ++visibleCount;

            ImGui::PushID(static_cast<int>(index));
            if (ID3D11ShaderResourceView* icon = IconTextureFor(info)) {
                ImGui::Image(ToTextureId(icon), ImVec2(16.0f * dpiScale_, 16.0f * dpiScale_));
                ImGui::SameLine();
            } else {
                ImGui::Dummy(ImVec2(18.0f * dpiScale_, 16.0f * dpiScale_));
                ImGui::SameLine();
            }

            std::wstring label = info.imageName + L"   (PID " + std::to_wstring(info.processId) + L")";
            if (info.isElevated) {
                label += L"  ";
                label += WideFromUtf8(text.badgeAdmin);
            }
            if (info.hasVisibleWindow) {
                label += L"  ";
                label += WideFromUtf8(text.badgeWindow);
            }

            const bool isSelected = static_cast<int>(index) == state.selectedProcessIndex;
            if (ImGui::Selectable(Narrow(label).c_str(), isSelected)) {
                state.selectedProcessIndex = static_cast<int>(index);
                requests.applyProcessSelection = true;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", Narrow(info.imagePath).c_str());
            }
            ImGui::PopID();
        }
        if (visibleCount == 0) {
            ImGui::TextDisabled("%s", text.noMatch);
        }
        ImGui::EndCombo();
    }

    if (!state.processListError.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s",
                           Narrow(state.processListError).c_str());
        ImGui::PopTextWrapPos();
    } else {
        ImGui::TextDisabled(text.listAgeFormat, state.processListAgeSeconds);
    }
}

void UIManager::DrawNetworkSection(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::SeparatorText(text.sectionNetwork);

    ImGui::Checkbox(text.blockOnStartup, &state.blockOnStartup);

    ImGui::Checkbox(text.terminateExisting, &state.terminateExistingConnections);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.terminateExistingTip);
    }
    (void)requests;
}

void UIManager::DrawHotkeySection(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::SeparatorText(text.sectionHotkeys);

    DrawHotkeyEditor(text.hotkeyBlock, text.hotkeyBlockTip,
                     HotkeyAction::ToggleBlocking, state.toggleBlocking, requests);
    DrawHotkeyEditor(text.hotkeyClickThrough, text.hotkeyClickThroughTip,
                     HotkeyAction::ToggleClickThrough, state.toggleClickThrough, requests);
    DrawHotkeyEditor(text.hotkeyOverlay, text.hotkeyOverlayTip,
                     HotkeyAction::ToggleOverlay, state.toggleOverlay, requests);
    DrawHotkeyEditor(text.hotkeyBorder, text.hotkeyBorderTip,
                     HotkeyAction::ToggleBorder, state.toggleBorder, requests);

    if (capture_.active()) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", text.hotkeyWaiting);
    }
    if (!captureMessage_.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "%s", Narrow(captureMessage_).c_str());
        ImGui::PopTextWrapPos();
    }
    if (!state.hotkeyError.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", Narrow(state.hotkeyError).c_str());
        ImGui::PopTextWrapPos();
    }
}

void UIManager::DrawHotkeyEditor(const char* label, const char* tooltip,
                                 HotkeyAction action, const HotkeyBinding& binding,
                                 UiRequests& requests)
{
    (void)requests;
    const UiStrings& text = Strings();
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(210.0f * dpiScale_);

    const bool isCapturing = capture_.active() && capture_.action() == action;
    const std::wstring caption = isCapturing
                                     ? WideFromUtf8(text.hotkeyWaiting)
                                     : FormatHotkeyText(binding.modifiers, binding.virtualKey);

    if (ImGui::Button(Narrow(caption).c_str(), ImVec2(200.0f * dpiScale_, 0.0f))) {
        capture_.Begin(action, true);
        captureMessage_.clear();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\n%s", tooltip, text.hotkeyClickHint);
    }
    ImGui::PopID();
}

void UIManager::UpdateHotkeyCapture(UiState& state, UiRequests& requests)
{
    if (!capture_.active()) {
        return;
    }

    HotkeyBinding captured;
    switch (capture_.Poll(captured)) {
        case HotkeyCaptureResult::Captured:
            requests.rebindHotkey = true;
            requests.rebindAction = capture_.action();
            requests.reboundBinding = captured;
            captureMessage_.clear();
            break;

        case HotkeyCaptureResult::Cancelled:
            captureMessage_ = WideFromUtf8(Strings().hotkeyCancelled);
            break;

        case HotkeyCaptureResult::NeedModifier:
            captureMessage_ = WideFromUtf8(Sprintf(Strings().hotkeyNeedModifier,
                                                   Utf8FromWide(FormatHotkeyText(
                                                       captured.modifiers,
                                                       captured.virtualKey)).c_str()));
            break;

        case HotkeyCaptureResult::Idle:
        default:
            break;
    }
    (void)state;
}

void UIManager::DrawWindowSection(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::SeparatorText(text.sectionWindow);

    if (ImGui::Checkbox(text.clickThrough, &state.clickThrough)) {
        requests.toggleClickThrough = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.clickThroughTip);
    }

    if (ImGui::Checkbox(text.alwaysOnTop, &state.topmost)) {
        requests.toggleTopmost = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox(text.showInTaskbar, &state.showInTaskbar)) {
        requests.toggleShowInTaskbar = true;
    }

    if (ImGui::Checkbox(text.gameMode, &state.noActivate)) {
        requests.toggleNoActivate = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.gameModeTip);
    }

    if (ImGui::Checkbox(text.excludeFromCapture, &state.excludeFromCapture)) {
        requests.toggleExcludeFromCapture = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.excludeFromCaptureTip);
    }

    if (ImGui::Button(text.resetPosition)) {
        requests.resetWindowPosition = true;
    }

    if (state.clickThrough) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), text.clickThroughWarning,
                           Narrow(FormatHotkeyText(state.toggleClickThrough.modifiers,
                                                   state.toggleClickThrough.virtualKey)).c_str());
        ImGui::PopTextWrapPos();
    }
}

void UIManager::DrawBorderSection(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::SeparatorText(text.sectionBorder);

    ImGui::Checkbox(text.borderEnabled, &state.borderEnabled);
    ImGui::SameLine();
    ImGui::Checkbox(text.borderGlow, &state.borderGlow);

    ImGui::SetNextItemWidth(190.0f * dpiScale_);
    ImGui::SliderFloat(text.borderThickness, &state.borderThickness, 1.0f, 16.0f, "%.0f px");
    ImGui::SetNextItemWidth(190.0f * dpiScale_);
    ImGui::SliderFloat(text.borderHueSpeed, &state.hueSpeed, 0.0f, 2.0f, "%.2f");
    ImGui::SetNextItemWidth(190.0f * dpiScale_);
    ImGui::SliderFloat(text.borderSaturation, &state.borderSaturation, 0.0f, 1.0f, "%.2f");
    ImGui::SetNextItemWidth(190.0f * dpiScale_);
    ImGui::SliderFloat(text.borderBrightness, &state.borderValue, 0.2f, 1.0f, "%.2f");
    (void)requests;
}

void UIManager::DrawGifSection(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::SeparatorText(text.sectionGif);

    ImGui::Checkbox(text.gifEnabled, &state.gifEnabled);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", text.gifEnabledTip);
    }

    // Two-way sync: the edit buffer is authoritative only while typing.
    if (gifPathBufferOwner_ != state.gifPath) {
        gifPathBufferOwner_ = state.gifPath;
        const std::string utf8 = Narrow(state.gifPath);
        std::snprintf(gifPathBuffer_, sizeof(gifPathBuffer_), "%s", utf8.c_str());
    }

    ImGui::BeginDisabled(state.noActivate);
    ImGui::SetNextItemWidth(260.0f * dpiScale_);
    if (ImGui::InputTextWithHint("##gif_path", text.gifPathHint, gifPathBuffer_,
                                 sizeof(gifPathBuffer_),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        state.gifPath = WideFromUtf8(gifPathBuffer_);
        gifPathBufferOwner_ = state.gifPath;
        requests.reloadGif = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button(text.browse)) {
        std::wstring chosen;
        if (BrowseForGifFile(chosen)) {
            state.gifPath = chosen;
            gifPathBufferOwner_.clear();
            requests.reloadGif = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(text.reload)) {
        requests.reloadGif = true;
    }

    ImGui::SetNextItemWidth(190.0f * dpiScale_);
    ImGui::SliderFloat(text.gifScale, &state.gifScale, 0.25f, 3.0f, "%.2fx");

    if (state.gifLoaded) {
        ImGui::TextDisabled(text.gifLoadedFormat, state.gifWidth, state.gifHeight,
                            state.gifFrameCount);
    } else if (!state.gifPath.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.3f, 1.0f), "%s", text.gifNotLoaded);
    }
    if (!state.gifError.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", Narrow(state.gifError).c_str());
        ImGui::PopTextWrapPos();
    }
}

void UIManager::DrawFooter(UiState& state, UiRequests& requests)
{
    const UiStrings& text = Strings();
    ImGui::Separator();

    if (ImGui::Button(text.saveSettings, ImVec2(160.0f * dpiScale_, 0.0f))) {
        requests.saveConfig = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(text.reloadSettings, ImVec2(140.0f * dpiScale_, 0.0f))) {
        requests.reloadConfig = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(text.openLogFolder, ImVec2(-FLT_MIN, 0.0f))) {
        const std::wstring directory = ParentDirectory(state.logPath);
        if (!directory.empty()) {
            ::ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    ImGui::TextDisabled(text.statsFormat, state.framesPerSecond, state.uptimeSeconds);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(text.statsTip, Narrow(state.configPath).c_str(),
                          Narrow(state.logPath).c_str());
    }
}

// ---------------------------------------------------------------------------
//  Shell file dialog (documented COM API, no third-party code)
// ---------------------------------------------------------------------------

bool UIManager::BrowseForGifFile(std::wstring& outPath)
{
    outPath.clear();

    ComPtr<IFileOpenDialog> dialog;
    HRESULT hr = ::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&dialog));
    if (FAILED(hr)) {
        LogWarn(logger_, "UI", Sprintf("CoCreateInstance(FileOpenDialog) failed (0x%08lX)",
                                       static_cast<unsigned long>(hr)));
        return false;
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    }

    const COMDLG_FILTERSPEC filters[] = {
        {L"GIF animation", L"*.gif"},
        {L"All files", L"*.*"},
    };
    dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
    dialog->SetTitle(L"Select a GIF animation");

    // Owned by this window so the dialog is drawn above the always-on-top panel
    // instead of behind it.
    hr = dialog->Show(window_);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IShellItem> item;
    hr = dialog->GetResult(&item);
    if (FAILED(hr)) {
        return false;
    }

    PWSTR rawPath = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
    if (FAILED(hr) || rawPath == nullptr) {
        return false;
    }
    outPath.assign(rawPath);
    ::CoTaskMemFree(rawPath);
    return true;
}

// ---------------------------------------------------------------------------
//  Texture caches
// ---------------------------------------------------------------------------

ID3D11ShaderResourceView* UIManager::IconTextureFor(const ProcessInfo& info)
{
    if (info.iconRgba.empty() || info.imagePath.empty() || !renderer_.initialized()) {
        return nullptr;
    }
    const auto cached = iconTextures_.find(info.imagePath);
    if (cached != iconTextures_.end()) {
        return cached->second;
    }
    ID3D11ShaderResourceView* view = renderer_.CreateRgbaTexture(
        info.iconRgba.data(), info.iconWidth, info.iconHeight);
    iconTextures_.emplace(info.imagePath, view);
    return view;
}

ID3D11ShaderResourceView* UIManager::GifTextureFor(std::size_t frameIndex)
{
    if (!renderer_.initialized()) {
        return nullptr;
    }
    if (gifTextureOwner_ != gifAnimator_.path()) {
        for (auto& [index, view] : gifTextures_) {
            (void)index;
            renderer_.ReleaseTexture(view);
        }
        gifTextures_.clear();
        gifTextureOwner_ = gifAnimator_.path();
    }

    const auto cached = gifTextures_.find(frameIndex);
    if (cached != gifTextures_.end()) {
        return cached->second;
    }

    const GifFrame* frame = gifAnimator_.CurrentFrame();
    if (frame == nullptr || frameIndex != gifAnimator_.currentIndex()) {
        return nullptr;
    }

    ID3D11ShaderResourceView* view = renderer_.CreateRgbaTexture(
        frame->rgba.data(), gifAnimator_.width(), gifAnimator_.height());
    gifTextures_.emplace(frameIndex, view);
    return view;
}

void UIManager::ReleaseTextureCache()
{
    if (renderer_.initialized()) {
        for (auto& [path, view] : iconTextures_) {
            (void)path;
            renderer_.ReleaseTexture(view);
        }
        for (auto& [index, view] : gifTextures_) {
            (void)index;
            renderer_.ReleaseTexture(view);
        }
    }
    iconTextures_.clear();
    gifTextures_.clear();
    gifTextureOwner_.clear();
}

} // namespace universalfnaf
