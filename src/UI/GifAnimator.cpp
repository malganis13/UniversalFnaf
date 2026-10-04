#include "universalfnaf/UI/GifAnimator.h"

#include "universalfnaf/Common/GdiPlusSession.h"
#include "universalfnaf/Common/Logger.h"
#include "universalfnaf/Common/Utils.h"
#include "universalfnaf/Common/WinError.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <memory>

namespace universalfnaf {
namespace {

// Safety limits so a hostile or simply enormous GIF cannot exhaust memory.
constexpr UINT kMaxFrames = 512;
constexpr int kMaxDimension = 4096;
constexpr std::size_t kMaxTotalBytes = 256ull * 1024ull * 1024ull;

constexpr int kMinimumFrameDelayMs = 20;

struct ImageDeleter {
    void operator()(Gdiplus::Image* image) const noexcept { delete image; }
};
using UniqueImage = std::unique_ptr<Gdiplus::Image, ImageDeleter>;

} // namespace

GifAnimator::GifAnimator(const GdiPlusSession& gdiPlus, ILogger& logger)
    : gdiPlus_(gdiPlus), logger_(logger)
{
}

GifAnimator::~GifAnimator() = default;

void GifAnimator::Clear()
{
    frames_.clear();
    path_.clear();
    index_ = 0;
    accumulatorMilliseconds_ = 0.0;
    width_ = 0;
    height_ = 0;
}

const GifFrame* GifAnimator::CurrentFrame() const noexcept
{
    if (frames_.empty()) {
        return nullptr;
    }
    return &frames_[index_];
}

void GifAnimator::Restart() noexcept
{
    index_ = 0;
    accumulatorMilliseconds_ = 0.0;
}

void GifAnimator::Update(double deltaSeconds)
{
    if (!playing_ || frames_.size() < 2 || deltaSeconds <= 0.0) {
        return;
    }

    accumulatorMilliseconds_ += deltaSeconds * 1000.0;

    // Guard against a huge delta (a stalled frame or a debugger break) looping
    // through the animation hundreds of times.
    int transitions = 0;
    while (accumulatorMilliseconds_ >= static_cast<double>(frames_[index_].delayMilliseconds) &&
           transitions < 16) {
        accumulatorMilliseconds_ -= static_cast<double>(frames_[index_].delayMilliseconds);
        index_ = (index_ + 1) % frames_.size();
        ++transitions;
    }
}

bool GifAnimator::Load(const std::wstring& path, std::wstring& error)
{
    error.clear();
    Clear();

    if (!gdiPlus_.ok()) {
        error = L"GDI+ is not initialized";
        return false;
    }
    if (!FileExists(path)) {
        error = L"GIF file not found: " + path;
        return false;
    }

    UniqueImage image(Gdiplus::Image::FromFile(path.c_str()));
    if (image == nullptr || image->GetLastStatus() != Gdiplus::Ok) {
        error = L"GDI+ could not decode the image: " + path;
        return false;
    }

    const int imageWidth = static_cast<int>(image->GetWidth());
    const int imageHeight = static_cast<int>(image->GetHeight());
    if (imageWidth <= 0 || imageHeight <= 0) {
        error = L"GIF has invalid dimensions";
        return false;
    }
    if (imageWidth > kMaxDimension || imageHeight > kMaxDimension) {
        error = L"GIF is too large (limit 4096x4096)";
        return false;
    }

    // GDI+ C++ wrapper returns these by value (see gdiplusheaders.h):
    //   UINT   GetFrameDimensionsCount();
    //   Status GetFrameDimensionsList(GUID*, UINT);
    //   UINT   GetFrameCount(const GUID*);
    const UINT dimensionCount = image->GetFrameDimensionsCount();
    if (dimensionCount == 0) {
        error = L"GIF frame dimensions are unavailable";
        return false;
    }
    std::vector<GUID> dimensions(dimensionCount);
    if (image->GetFrameDimensionsList(dimensions.data(), dimensionCount) != Gdiplus::Ok) {
        error = L"GIF frame dimensions could not be read";
        return false;
    }
    const GUID& frameDimension = dimensions[0];

    UINT frameCount = image->GetFrameCount(&frameDimension);
    if (frameCount == 0) {
        frameCount = 1;   // static image with a .gif extension
    }
    frameCount = std::min<UINT>(frameCount, kMaxFrames);

    // --- per-frame delays (GIF stores them in units of 1/100 second) --------
    std::vector<int> delays(frameCount, 100);
    const UINT propertySize = image->GetPropertyItemSize(PropertyTagFrameDelay);
    if (propertySize >= sizeof(Gdiplus::PropertyItem)) {
        std::vector<std::uint8_t> buffer(propertySize);
        auto* property = reinterpret_cast<Gdiplus::PropertyItem*>(buffer.data());
        if (image->GetPropertyItem(PropertyTagFrameDelay, propertySize, property) == Gdiplus::Ok &&
            property->value != nullptr && property->length >= sizeof(long)) {
            const auto* values = static_cast<const long*>(property->value);
            const std::size_t valueCount = property->length / sizeof(long);
            for (std::size_t i = 0; i < frameCount && i < valueCount; ++i) {
                delays[i] = values[i] > 0 ? static_cast<int>(values[i]) * 10 : 100;
            }
        }
    }
    for (int& delay : delays) {
        delay = std::max(delay, kMinimumFrameDelayMs);
    }

    // --- decode every frame onto one cumulative canvas ---------------------
    // PixelFormat32bppARGB is a macro, so it must not be qualified with Gdiplus::.
    Gdiplus::Bitmap canvas(imageWidth, imageHeight, PixelFormat32bppARGB);
    if (canvas.GetLastStatus() != Gdiplus::Ok) {
        error = L"Could not allocate the GIF composition canvas";
        return false;
    }
    {
        Gdiplus::Graphics clearGraphics(&canvas);
        clearGraphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    }

    const std::size_t bytesPerFrame =
        static_cast<std::size_t>(imageWidth) * static_cast<std::size_t>(imageHeight) * 4u;
    frames_.reserve(std::min<std::size_t>(frameCount, kMaxTotalBytes / std::max<std::size_t>(bytesPerFrame, 1u)));

    for (UINT frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        if (image->SelectActiveFrame(&frameDimension, frameIndex) != Gdiplus::Ok) {
            break;
        }
        {
            Gdiplus::Graphics graphics(&canvas);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            graphics.DrawImage(image.get(), 0, 0, 0, 0, imageWidth, imageHeight,
                               Gdiplus::UnitPixel);
        }

        Gdiplus::Rect rect(0, 0, imageWidth, imageHeight);
        Gdiplus::BitmapData locked{};
        if (canvas.LockBits(&rect, Gdiplus::ImageLockModeRead,
                            PixelFormat32bppARGB, &locked) != Gdiplus::Ok) {
            break;
        }

        GifFrame frame;
        frame.delayMilliseconds = delays[frameIndex];
        GdiPlusSession::CopyBgraPremultipliedToStraightRgba(
            static_cast<const std::uint8_t*>(locked.Scan0),
            static_cast<int>(locked.Stride),
            imageWidth, imageHeight, frame.rgba);
        canvas.UnlockBits(&locked);

        if (frames_.size() * bytesPerFrame + frame.rgba.size() > kMaxTotalBytes) {
            LogWarn(logger_, "GIF", Sprintf("animation truncated at %zu frames (memory limit)",
                                            frames_.size()));
            break;
        }
        frames_.push_back(std::move(frame));
    }

    if (frames_.empty()) {
        error = L"No frames could be decoded from: " + path;
        return false;
    }

    path_ = path;
    width_ = imageWidth;
    height_ = imageHeight;
    index_ = 0;
    accumulatorMilliseconds_ = 0.0;

    LogInfo(logger_, "GIF", Sprintf("loaded '%s' %dx%d frames=%zu",
                                    Utf8FromWide(path).c_str(), width_, height_, frames_.size()));
    return true;
}

} // namespace universalfnaf
