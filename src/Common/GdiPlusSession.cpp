#include "universalfnaf/Common/GdiPlusSession.h"

#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"

#include <objidl.h>
#include <gdiplus.h>

namespace universalfnaf {

GdiPlusSession::GdiPlusSession()
{
    Gdiplus::GdiplusStartupInput input;
    input.GdiplusVersion = 1;
    input.DebugEventCallback = nullptr;
    input.SuppressBackgroundThread = FALSE;
    input.SuppressExternalCodecs = FALSE;

    const Gdiplus::Status status = Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    if (status != Gdiplus::Ok) {
        token_ = 0;
        error_ = L"GdiplusStartup failed";
    }
}

GdiPlusSession::~GdiPlusSession()
{
    if (token_ != 0) {
        Gdiplus::GdiplusShutdown(token_);
        token_ = 0;
    }
}

void GdiPlusSession::CopyBgraPremultipliedToStraightRgba(const std::uint8_t* source,
                                                         int sourceStride,
                                                         int width,
                                                         int height,
                                                         std::vector<std::uint8_t>& outPixels)
{
    outPixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    if (source == nullptr || width <= 0 || height <= 0) {
        return;
    }

    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = source + static_cast<std::ptrdiff_t>(y) * sourceStride;
        std::uint8_t* dst = outPixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        for (int x = 0; x < width; ++x) {
            const std::uint8_t b = row[x * 4 + 0];
            const std::uint8_t g = row[x * 4 + 1];
            const std::uint8_t r = row[x * 4 + 2];
            const std::uint8_t a = row[x * 4 + 3];

            std::uint8_t outR = r;
            std::uint8_t outG = g;
            std::uint8_t outB = b;
            if (a != 0u && a != 255u) {
                // Undo GDI+'s premultiplication so ImGui's SRC_ALPHA blending is correct.
                outR = static_cast<std::uint8_t>((static_cast<unsigned>(r) * 255u + a / 2u) / a);
                outG = static_cast<std::uint8_t>((static_cast<unsigned>(g) * 255u + a / 2u) / a);
                outB = static_cast<std::uint8_t>((static_cast<unsigned>(b) * 255u + a / 2u) / a);
            }

            dst[x * 4 + 0] = outR;
            dst[x * 4 + 1] = outG;
            dst[x * 4 + 2] = outB;
            dst[x * 4 + 3] = a;
        }
    }
}

bool GdiPlusSession::IconToRgba(HICON icon,
                                std::vector<std::uint8_t>& outPixels,
                                int& outWidth,
                                int& outHeight)
{
    outPixels.clear();
    outWidth = 0;
    outHeight = 0;
    if (icon == nullptr) {
        return false;
    }

    Gdiplus::Bitmap* bitmap = Gdiplus::Bitmap::FromHICON(icon);
    if (bitmap == nullptr) {
        return false;
    }
    const auto bitmapGuard = MakeScopeGuard([bitmap]() noexcept { delete bitmap; });

    const int width = static_cast<int>(bitmap->GetWidth());
    const int height = static_cast<int>(bitmap->GetHeight());
    if (width <= 0 || height <= 0) {
        return false;
    }

    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data{};
    // PixelFormat32bppARGB is a macro, not an enumerator: no Gdiplus:: prefix.
    if (bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead,
                         PixelFormat32bppARGB, &data) != Gdiplus::Ok) {
        return false;
    }

    CopyBgraPremultipliedToStraightRgba(static_cast<const std::uint8_t*>(data.Scan0),
                                        static_cast<int>(data.Stride),
                                        width, height, outPixels);
    bitmap->UnlockBits(&data);

    // Legacy icons carry no alpha channel at all: GDI+ reports alpha == 0 for
    // every pixel, which would make the icon fully invisible. Detect and repair.
    bool anyAlpha = false;
    for (std::size_t i = 3; i < outPixels.size(); i += 4) {
        if (outPixels[i] != 0u) {
            anyAlpha = true;
            break;
        }
    }
    if (!anyAlpha) {
        for (std::size_t i = 3; i < outPixels.size(); i += 4) {
            outPixels[i] = 255u;
        }
    }

    outWidth = width;
    outHeight = height;
    return true;
}

} // namespace universalfnaf
