#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Common/GdiPlusSession.h
//  RAII owner of the GDI+ subsystem plus the pixel-format helpers used by both
//  the GIF decoder and the process-icon extractor.
//
//  GDI+ is used strictly as a *decoder* (GIF frames, process icons). Nothing
//  here renders into another application's window or device context.
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace universalfnaf {

class GdiPlusSession {
public:
    GdiPlusSession();
    ~GdiPlusSession();

    GdiPlusSession(const GdiPlusSession&) = delete;
    GdiPlusSession& operator=(const GdiPlusSession&) = delete;
    GdiPlusSession(GdiPlusSession&&) = delete;
    GdiPlusSession& operator=(GdiPlusSession&&) = delete;

    bool ok() const noexcept { return token_ != 0; }
    const std::wstring& error() const noexcept { return error_; }

    // HICON -> tightly packed, top-down RGBA8 (DXGI_FORMAT_R8G8B8A8_UNORM).
    // The icon is copied, never modified; the caller keeps ownership of HICON.
    static bool IconToRgba(HICON icon,
                           std::vector<std::uint8_t>& outPixels,
                           int& outWidth,
                           int& outHeight);

    // GDI+ hands 32bppARGB buffers back premultiplied and in BGRA byte order.
    // This converts them to straight-alpha RGBA8 for D3D11/ImGui blending.
    static void CopyBgraPremultipliedToStraightRgba(const std::uint8_t* source,
                                                    int sourceStride,
                                                    int width,
                                                    int height,
                                                    std::vector<std::uint8_t>& outPixels);

private:
    ULONG_PTR token_ = 0;
    std::wstring error_;
};

} // namespace universalfnaf
