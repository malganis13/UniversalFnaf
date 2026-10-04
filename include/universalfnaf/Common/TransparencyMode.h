#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Common/TransparencyMode.h
//  Kept in its own header so that Core/Config can describe the overlay mode
//  without depending on the DirectX 11 renderer header (dependency inversion).
// ---------------------------------------------------------------------------

namespace universalfnaf {

enum class TransparencyMode {
    DirectComposition = 0,   // per-pixel alpha through DWM (default)
    ColorKeyLayered,         // WS_EX_LAYERED + LWA_COLORKEY fallback
};

inline const wchar_t* TransparencyModeName(TransparencyMode mode) noexcept
{
    switch (mode) {
        case TransparencyMode::DirectComposition: return L"DirectComposition (per-pixel alpha)";
        case TransparencyMode::ColorKeyLayered:   return L"Layered window (colour key)";
    }
    return L"Unknown";
}

} // namespace universalfnaf
